#ifndef NTP_TIME_SYNC_H
#define NTP_TIME_SYNC_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>

namespace ntp {

/**
 * @brief NTP时间同步管理器，支持周期性后台同步
 */
class NtpSync {
public:
    NtpSync();
    ~NtpSync();

    // 禁止拷贝
    NtpSync(const NtpSync&) = delete;
    NtpSync& operator=(const NtpSync&) = delete;

    /**
     * @brief 启动周期性同步
     * @param intervalSec 同步间隔（秒），默认3600秒（1小时）
     * @param timeoutMs   单次同步超时时间（毫秒）
     * @param maxRetries  单次同步最大重试次数
     * @return true 启动成功
     */
    bool startPeriodicSync(int intervalSec = 3600, int timeoutMs = 3000, int maxRetries = 2);

    /**
     * @brief 停止周期性同步
     */
    void stop();

    /**
     * @brief 立即执行一次时间同步（阻塞）
     * @return true 同步成功
     */
    bool syncNow();

    /**
     * @brief 检查系统时间是否可信（至少成功同步过一次，且距离上次同步不超过2个周期）
     */
    bool isTimeReliable() const;

    /**
     * @brief 获取最后一次成功同步的时间戳（系统时间）
     */
    std::chrono::system_clock::time_point getLastSyncTime() const;

private:
    void periodicSyncLoop(int intervalSec, int timeoutMs, int maxRetries);

    std::atomic<bool> running_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::chrono::system_clock::time_point lastSyncTime_;
    bool everSynced_;
};



} // namespace ntp

#endif // NTP_TIME_SYNC_H