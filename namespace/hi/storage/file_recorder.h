#ifndef FILE_RECORDER_H
#define FILE_RECORDER_H

#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <atomic>
#include <vector>

namespace hisi {
namespace storage {

/**
 * @brief H.264码流文件录制器，支持按时间周期切分文件、按磁盘剩余空间自动删除旧文件。
 *
 * 特性：
 * - 每个文件录制固定时长（durationSec），到达后自动切换新文件。
 * - 文件名包含起始时间戳，格式：baseName_YYYYMMDD_HHMMSS.h264。
 * - 当磁盘剩余空间低于 minFreeSpaceMB 时，自动删除最旧的录制文件。
 * - 系统时间不可靠时，通过解析已有文件名中的最大时间戳来生成下一个文件名。
 */
class FileRecorder {
public:
    /**
     * @param outputDir       输出目录（必须存在或可创建）
     * @param baseName        文件名前缀
     * @param durationSec     每个文件的录制时长（秒）
     * @param minFreeSpaceMB  最小剩余空间（MB），低于此值时会删除旧文件
     * @param maxFileCount    最大保留文件数量（0表示不限制，仅由空间决定）
     * @param timeReliableCb  可选回调，用于判断系统时间是否可靠，若不提供则默认认为可靠
     */
    FileRecorder(const std::string& outputDir,
                 const std::string& baseName,
                 int durationSec,
                 int minFreeSpaceMB,
                 size_t maxFileCount = 0,
                 std::function<bool()> timeReliableCb = nullptr);
    ~FileRecorder();

    // 禁止拷贝
    FileRecorder(const FileRecorder&) = delete;
    FileRecorder& operator=(const FileRecorder&) = delete;

    /**
     * @brief 写入一帧H.264数据
     * @param data       码流数据（可带或不带起始码，自动处理）
     * @param len        数据长度
     * @param isKeyFrame 是否为关键帧（暂未使用）
     * @return true 写入成功
     */
    bool writeFrame(const uint8_t* data, size_t len, bool isKeyFrame);

private:
    // 切换文件（关闭当前，打开新文件）
    bool rotateFile();
    // 根据当前时间生成文件名（若时间不可靠则基于已有文件推算）
    std::string generateFilePath();
    // 获取目录下所有匹配的文件，按时间戳排序（从旧到新）
    std::vector<std::string> getSortedFiles() const;
    // 解析文件名中的时间戳（返回自纪元起的秒数）
    int64_t parseTimestampFromFilename(const std::string& filename) const;
    // 检查并清理磁盘空间：若剩余空间低于阈值，删除最旧文件直至空间足够
    void enforceFreeSpace();
    // 获取指定路径的剩余空间（MB）
    int getFreeSpaceMB(const std::string& path) const;
    // 尝试创建目录
    void ensureDir() const;

    std::string outputDir_;
    std::string baseName_;
    int durationSec_;
    int minFreeSpaceMB_;
    size_t maxFileCount_;
    std::function<bool()> timeReliableCb_;

    std::ofstream fileStream_;
    std::string currentFilePath_;
    std::chrono::steady_clock::time_point fileStartTime_;  // 用于计时（单调时钟）
    int64_t fileStartRealTime_;  // 系统绝对时间（秒），用于生成文件名
    size_t frameCount_;          // 辅助帧率估算（可选）
    mutable std::mutex mutex_;
};

} // namespace storage
} // namespace hisi

#endif // FILE_RECORDER_H