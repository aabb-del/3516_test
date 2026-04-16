#ifndef HISI_STORAGE_FILE_RECORDER_H
#define HISI_STORAGE_FILE_RECORDER_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <chrono>
#include <fstream>

namespace hisi {
namespace storage {

class FileRecorder {
public:
    /**
     * @param outputDir      输出目录
     * @param baseName       文件名前缀
     * @param durationSec    每个文件的录制时长(秒)
     * @param minFreeSpaceMB 最小剩余空间(MB)，低于此值将删除旧文件
     * @param maxFileCount   最大文件数量，0表示不限制
     * @param timeReliableCb 回调函数，返回true表示系统时间可靠（例如NTP已同步）
     */
    FileRecorder(const std::string& outputDir,
                 const std::string& baseName,
                 int durationSec,
                 int minFreeSpaceMB = 100,
                 size_t maxFileCount = 0,
                 std::function<bool()> timeReliableCb = nullptr);

    ~FileRecorder();

    // 写入一帧H.264 NALU数据（不包含起始码，函数内自动添加）
    bool writeFrame(const uint8_t* data, size_t len, bool isKeyFrame = false);

private:
    bool rotateFile();                          // 切换文件
    std::string generateFilePath();             // 生成新文件路径（使用本地时间）
    std::vector<std::string> getSortedFiles() const; // 按文件名中时间戳排序的文件列表
    int64_t parseTimestampFromFilename(const std::string& fullpath) const; // 从文件名解析本地时间戳
    void enforceFreeSpace();                    // 删除旧文件以满足空间/数量限制
    int getFreeSpaceMB(const std::string& path) const; // 获取剩余空间(MB)
    void ensureDir() const;                     // 递归创建目录

    // 本地时间转换辅助函数（受TZ环境变量影响）
    static std::string timestampToLocalString(int64_t ts);
    static int64_t localStringToTimestamp(const std::string& str);

    const std::string outputDir_;
    const std::string baseName_;
    const int durationSec_;
    const int minFreeSpaceMB_;
    const size_t maxFileCount_;
    std::function<bool()> timeReliableCb_;

    std::mutex mutex_;
    std::ofstream fileStream_;
    std::string currentFilePath_;
    std::chrono::steady_clock::time_point fileStartTime_;  // 单调时钟起点
    bool failed_;                                          // 初始化是否失败
    int frameCounter_;                                     // 帧计数器，用于周期性检查空间
};

} // namespace storage
} // namespace hisi

#endif // HISI_STORAGE_FILE_RECORDER_H