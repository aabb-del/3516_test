#include "file_recorder.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <errno.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

#include <fcntl.h>           /* Definition of AT_* constants */
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <libgen.h>  // 可选，也可用字符串操作

namespace hisi {
namespace storage {

static bool mkdirRecursive(const std::string& path, mode_t mode) {
    if (path.empty()) return false;
    // 如果已存在，直接成功
    if (access(path.c_str(), F_OK) == 0) return true;
    
    // 递归创建父目录
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos != 0) {
        std::string parent = path.substr(0, pos);
        if (!mkdirRecursive(parent, mode)) return false;
    }
    // 创建当前目录
    if (mkdir(path.c_str(), mode) != 0 && errno != EEXIST) {
        std::cerr << "mkdir failed: " << path << " - " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}


// 写入时自动添加起始码（如果缺失）
static bool writeNaluWithStartCode(std::ofstream& os, const uint8_t* data, size_t len) {
    if (!os.good()) return false;
    bool hasStartCode = false;
    if (len >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01)
        hasStartCode = true;
    else if (len >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)
        hasStartCode = true;
    if (!hasStartCode) {
        const uint8_t startCode[4] = {0x00, 0x00, 0x00, 0x01};
        os.write(reinterpret_cast<const char*>(startCode), 4);
        if (!os.good()) return false;
    }
    os.write(reinterpret_cast<const char*>(data), len);
    return os.good();
}

FileRecorder::FileRecorder(const std::string& outputDir,
                           const std::string& baseName,
                           int durationSec,
                           int minFreeSpaceMB,
                           size_t maxFileCount,
                           std::function<bool()> timeReliableCb)
    : outputDir_(outputDir),
      baseName_(baseName),
      durationSec_(durationSec),
      minFreeSpaceMB_(minFreeSpaceMB),
      maxFileCount_(maxFileCount),
      timeReliableCb_(timeReliableCb),
      fileStartRealTime_(0) {
    ensureDir();
    // 尝试打开第一个文件
    rotateFile();
}

FileRecorder::~FileRecorder() {
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

bool FileRecorder::writeFrame(const uint8_t* data, size_t len, bool /*isKeyFrame*/) {
    if (data == nullptr || len == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否需要切换文件（基于单调时钟的时长）
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - fileStartTime_).count();
    if (elapsed >= durationSec_) {
        if (!rotateFile()) {
            std::cerr << "Failed to rotate file, recording may stop." << std::endl;
            return false;
        }
    }

    if (!fileStream_.is_open()) return false;

    if (!writeNaluWithStartCode(fileStream_, data, len)) {
        std::cerr << "Failed to write data to file" << std::endl;
        return false;
    }
    fileStream_.flush();

    // 每写入一帧后检查一次磁盘空间（开销较小，也可以每N帧检查一次）
    enforceFreeSpace();

    return true;
}

bool FileRecorder::rotateFile() {
    // 关闭当前文件
    if (fileStream_.is_open()) {
        fileStream_.close();
        // 关闭后清理旧文件（基于空间/数量）
        enforceFreeSpace();
    }

    // 生成新文件路径
    currentFilePath_ = generateFilePath();
    fileStream_.open(currentFilePath_, std::ios::binary | std::ios::trunc);
    if (!fileStream_.is_open()) {
        std::cerr << "Failed to open file: " << currentFilePath_ << std::endl;
        return false;
    }

    // 重置计时起点
    fileStartTime_ = std::chrono::steady_clock::now();
    // 记录实际时间（用于生成下一次文件名，但此处已生成）
    fileStartRealTime_ = std::time(nullptr);
    std::cout << "Recording to new file: " << currentFilePath_ << std::endl;
    return true;
}

std::string FileRecorder::generateFilePath() {
    // 判断系统时间是否可靠
    bool timeReliable = true;
    if (timeReliableCb_) {
        timeReliable = timeReliableCb_();
    } else {
        // 默认检查当前时间是否大于一个合理的基准（如2020年1月1日）
        std::time_t now = std::time(nullptr);
        if (now < 1577836800) { // 2020-01-01 00:00:00 UTC
            timeReliable = false;
        }
    }

    std::string timestampStr;
    if (timeReliable) {
        // 使用当前系统时间
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y%m%d_%H%M%S");
        timestampStr = oss.str();
    } else {
        // 时间不可靠：从已有文件名中获取最大时间戳，加上 durationSec 作为新时间戳
        auto files = getSortedFiles();
        int64_t maxTimestamp = 0;
        for (const auto& f : files) {
            int64_t ts = parseTimestampFromFilename(f);
            if (ts > maxTimestamp) maxTimestamp = ts;
        }
        if (maxTimestamp == 0) {
            // 没有任何文件，使用一个基准时间（如0）
            timestampStr = "19700101_000000";
        } else {
            // 新文件的时间戳 = 最大时间戳 + durationSec
            maxTimestamp += durationSec_;
            std::time_t t = static_cast<std::time_t>(maxTimestamp);
            std::tm* tm = std::gmtime(&t);
            std::ostringstream oss;
            oss << std::put_time(tm, "%Y%m%d_%H%M%S");
            timestampStr = oss.str();
        }
    }

    // 构造完整路径
    return outputDir_ + "/" + baseName_ + "_" + timestampStr + ".h264";
}

std::vector<std::string> FileRecorder::getSortedFiles() const {
    std::vector<std::string> files;
    DIR* dir = opendir(outputDir_.c_str());
    if (!dir) return files;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // 匹配格式：baseName_YYYYMMDD_HHMMSS.h264
        if (name.find(baseName_ + "_") == 0 && name.find(".h264") == name.size() - 5) {
            files.push_back(outputDir_ + "/" + name);
        }
    }
    closedir(dir);
    // 按文件名中的时间戳排序（从旧到新）
    std::sort(files.begin(), files.end(),
              [this](const std::string& a, const std::string& b) {
                  int64_t ta = parseTimestampFromFilename(a);
                  int64_t tb = parseTimestampFromFilename(b);
                  return ta < tb;
              });
    return files;
}

int64_t FileRecorder::parseTimestampFromFilename(const std::string& fullpath) const {
    // 提取文件名部分
    size_t slash = fullpath.find_last_of('/');
    std::string fname = (slash == std::string::npos) ? fullpath : fullpath.substr(slash + 1);
    // 格式：baseName_YYYYMMDD_HHMMSS.h264
    if (fname.length() < baseName_.size() + 16) return 0;
    std::string timePart = fname.substr(baseName_.size() + 1, 15); // YYYYMMDD_HHMMSS
    if (timePart.size() != 15) return 0;
    std::tm tm = {};
    std::istringstream ss(timePart);
    ss >> std::get_time(&tm, "%Y%m%d_%H%M%S");
    if (ss.fail()) return 0;
    std::time_t t = std::mktime(&tm);
    return static_cast<int64_t>(t);
}

void FileRecorder::enforceFreeSpace() {
    // 1. 检查剩余空间
    int freeMB = getFreeSpaceMB(outputDir_);
    if (freeMB >= minFreeSpaceMB_ && (maxFileCount_ == 0 || getSortedFiles().size() <= maxFileCount_)) {
        return; // 空间足够且数量未超限
    }

    // 2. 需要删除旧文件
    auto files = getSortedFiles();
    if (files.empty()) return;

    // 如果因数量超限需要删除，则删除最旧的文件直到数量 ≤ maxFileCount_
    while (maxFileCount_ > 0 && files.size() > maxFileCount_) {
        const std::string& oldest = files.front();
        if (remove(oldest.c_str()) == 0) {
            std::cout << "Deleted old file (exceed max count): " << oldest << std::endl;
            files.erase(files.begin());
        } else {
            std::cerr << "Failed to delete file: " << oldest << std::endl;
            break;
        }
    }

    // 3. 因空间不足需要删除
    while (freeMB < minFreeSpaceMB_ && !files.empty()) {
        const std::string& oldest = files.front();
        if (remove(oldest.c_str()) == 0) {
            std::cout << "Deleted old file (low disk space): " << oldest << std::endl;
            files.erase(files.begin());
            freeMB = getFreeSpaceMB(outputDir_); // 重新获取剩余空间
        } else {
            std::cerr << "Failed to delete file: " << oldest << std::endl;
            break;
        }
    }
}

int FileRecorder::getFreeSpaceMB(const std::string& path) const {
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        return 0;
    }
    // 可用块数 * 块大小 = 可用字节，转换为 MB
    uint64_t freeBytes = stat.f_bavail * stat.f_frsize;
    return static_cast<int>(freeBytes / (1024 * 1024));
}

void FileRecorder::ensureDir() const {
    if (!mkdirRecursive(outputDir_, 0755)) {
        std::cerr << "Failed to create directory: " << outputDir_ << std::endl;
    }
}

} // namespace storage
} // namespace hisi