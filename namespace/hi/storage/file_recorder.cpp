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
#include <unistd.h>

namespace hisi {
namespace storage {

static bool mkdirRecursive(const std::string& path, mode_t mode) {
    if (path.empty()) return false;
    if (access(path.c_str(), F_OK) == 0) return true;

    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos != 0) {
        std::string parent = path.substr(0, pos);
        if (!mkdirRecursive(parent, mode)) return false;
    }
    if (mkdir(path.c_str(), mode) != 0 && errno != EEXIST) {
        std::cerr << "mkdir failed: " << path << " - " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

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

// 将Unix时间戳转换为本地时间字符串 YYYYMMDD_HHMMSS
std::string FileRecorder::timestampToLocalString(int64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm tm;
    localtime_r(&t, &tm);   // 受TZ环境变量影响，这里会得到东八区时间
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

// 将本地时间字符串解析为Unix时间戳（受TZ环境变量影响）
int64_t FileRecorder::localStringToTimestamp(const std::string& str) {
    struct tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y%m%d_%H%M%S");
    if (ss.fail()) return 0;
    // mktime 会根据本地时区转换
    return static_cast<int64_t>(mktime(&tm));
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
      failed_(false),
      frameCounter_(0) {
    // 规范化输出目录：去除末尾的 '/'
    if (!outputDir_.empty() && outputDir_.back() == '/') {
        const_cast<std::string&>(outputDir_).pop_back();
    }
    ensureDir();
    if (!rotateFile()) {
        std::cerr << "FileRecorder: failed to create initial file" << std::endl;
        failed_ = true;
    }
}

FileRecorder::~FileRecorder() {
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

bool FileRecorder::writeFrame(const uint8_t* data, size_t len, bool /*isKeyFrame*/) {
    if (failed_ || data == nullptr || len == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查时长，是否需要切换文件
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - fileStartTime_).count();
    if (elapsed >= durationSec_) {
        if (!rotateFile()) {
            std::cerr << "FileRecorder: rotateFile failed, stop recording" << std::endl;
            failed_ = true;
            return false;
        }
    }

    if (!fileStream_.is_open()) return false;

    if (!writeNaluWithStartCode(fileStream_, data, len)) {
        std::cerr << "FileRecorder: write data failed" << std::endl;
        return false;
    }
    fileStream_.flush();

    // 每写入10帧检查一次磁盘空间（避免频繁stat）
    if (++frameCounter_ >= 10) {
        frameCounter_ = 0;
        enforceFreeSpace();
    }
    return true;
}

bool FileRecorder::rotateFile() {
    // 关闭当前文件
    if (fileStream_.is_open()) {
        fileStream_.close();
        // 关闭后清理旧文件
        enforceFreeSpace();
    }

    // 生成新文件路径
    currentFilePath_ = generateFilePath();
    fileStream_.open(currentFilePath_, std::ios::binary | std::ios::trunc);
    if (!fileStream_.is_open()) {
        std::cerr << "FileRecorder: failed to open file " << currentFilePath_
                  << ", error: " << strerror(errno) << std::endl;
        return false;
    }

    // 重置计时起点（单调时钟）
    fileStartTime_ = std::chrono::steady_clock::now();
    std::cout << "FileRecorder: recording to " << currentFilePath_ << std::endl;
    return true;
}

std::string FileRecorder::generateFilePath() {
    bool timeReliable = true;
    if (timeReliableCb_) {
        timeReliable = timeReliableCb_();
    } else {
        // 默认检查时间是否大于2020-01-01 00:00:00 UTC
        std::time_t now = std::time(nullptr);
        if (now < 1577836800) timeReliable = false;
    }

    int64_t timestamp = 0;
    if (timeReliable) {
        // 使用当前系统时间（UTC时间戳，但后续会转为本地时间字符串）
        timestamp = static_cast<int64_t>(std::time(nullptr));
    } else {
        // 从已有文件中获取最大时间戳，加上 durationSec
        auto files = getSortedFiles();
        int64_t maxTs = 0;
        for (const auto& f : files) {
            int64_t ts = parseTimestampFromFilename(f);
            if (ts > maxTs) maxTs = ts;
        }
        if (maxTs == 0) {
            // 无有效文件，使用一个基准时间（2020-01-01 00:00:00 本地时间）
            timestamp = 1577836800; // UTC 2020-01-01 00:00:00，转为本地时间字符串会加上时区偏移
        } else {
            timestamp = maxTs + durationSec_;
        }
    }
    std::string timeStr = timestampToLocalString(timestamp);
    return outputDir_ + "/" + baseName_ + "_" + timeStr + ".h264";
}

std::vector<std::string> FileRecorder::getSortedFiles() const {
    std::vector<std::string> files;
    DIR* dir = opendir(outputDir_.c_str());
    if (!dir) return files;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // 匹配 baseName_YYYYMMDD_HHMMSS.h264
        if (name.find(baseName_ + "_") == 0 && name.size() > baseName_.size() + 16 &&
            name.substr(name.size() - 5) == ".h264") {
            files.push_back(outputDir_ + "/" + name);
        }
    }
    closedir(dir);
    // 按文件名中的时间戳排序（升序，旧→新）
    std::sort(files.begin(), files.end(),
              [this](const std::string& a, const std::string& b) {
                  return parseTimestampFromFilename(a) < parseTimestampFromFilename(b);
              });
    return files;
}

int64_t FileRecorder::parseTimestampFromFilename(const std::string& fullpath) const {
    // 提取文件名
    size_t slash = fullpath.find_last_of('/');
    std::string fname = (slash == std::string::npos) ? fullpath : fullpath.substr(slash + 1);
    // 格式：baseName_YYYYMMDD_HHMMSS.h264
    if (fname.length() < baseName_.size() + 16) return 0;
    std::string timePart = fname.substr(baseName_.size() + 1, 15); // YYYYMMDD_HHMMSS
    if (timePart.size() != 15) return 0;
    return localStringToTimestamp(timePart);
}

void FileRecorder::enforceFreeSpace() {
    // 获取当前文件列表（已排序）
    auto files = getSortedFiles();
    if (files.empty()) return;

    // 1. 检查剩余空间
    int freeMB = getFreeSpaceMB(outputDir_);
    bool spaceOk = (freeMB >= minFreeSpaceMB_);
    bool countOk = (maxFileCount_ == 0 || files.size() <= maxFileCount_);
    if (spaceOk && countOk) return;

    // 2. 优先删除最旧文件直到满足数量限制
    while (maxFileCount_ > 0 && files.size() > maxFileCount_) {
        const std::string& oldest = files.front();
        if (remove(oldest.c_str()) == 0) {
            std::cout << "FileRecorder: deleted (exceed max count) " << oldest << std::endl;
            files.erase(files.begin());
        } else {
            std::cerr << "FileRecorder: failed to delete " << oldest << " - " << strerror(errno) << std::endl;
            break;
        }
    }

    // 3. 若空间仍不足，继续删除最旧文件
    while (freeMB < minFreeSpaceMB_ && !files.empty()) {
        const std::string& oldest = files.front();
        if (remove(oldest.c_str()) == 0) {
            std::cout << "FileRecorder: deleted (low space) " << oldest << std::endl;
            files.erase(files.begin());
            freeMB = getFreeSpaceMB(outputDir_);
        } else {
            std::cerr << "FileRecorder: failed to delete " << oldest << " - " << strerror(errno) << std::endl;
            break;
        }
    }
}

int FileRecorder::getFreeSpaceMB(const std::string& path) const {
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        std::cerr << "FileRecorder: statvfs failed on " << path << " - " << strerror(errno) << std::endl;
        return 0;
    }
    uint64_t freeBytes = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;
    return static_cast<int>(freeBytes / (1024 * 1024));
}

void FileRecorder::ensureDir() const {
    if (!mkdirRecursive(outputDir_, 0755)) {
        std::cerr << "FileRecorder: ensureDir failed for " << outputDir_ << std::endl;
    }
}

} // namespace storage
} // namespace hisi