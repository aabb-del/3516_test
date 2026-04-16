#ifndef NTP_TIME_SYNC_H
#define NTP_TIME_SYNC_H

#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>

namespace ntp {

/**
 * @brief NTP 时间同步类
 * 
 * 功能：
 * 1. 单次查询 NTP 服务器并设置系统时间
 * 2. 周期同步（后台线程）
 * 3. 判断系统时间是否可靠（基于上次同步时间）
 */
class NtpSync {
public:
    NtpSync();
    ~NtpSync();

    /**
     * @brief 启动周期性时间同步
     * @param intervalSec 同步间隔（秒）
     * @param timeoutMs   单次查询超时（毫秒）
     * @param maxRetries  单次查询最大重试次数
     * @return true 成功启动，false 线程已运行
     */
    bool startPeriodicSync(int intervalSec = 3600, int timeoutMs = 3000, int maxRetries = 2);

    /**
     * @brief 停止周期性同步
     */
    void stop();

    /**
     * @brief 立即同步一次（使用默认或指定的 NTP 服务器）
     * @param server NTP 服务器域名/IP，默认 "pool.ntp.org"
     * @param timeoutMs 超时（毫秒）
     * @param maxRetries 重试次数
     * @return true 同步成功并设置了系统时间
     */
    bool syncNow(const std::string& server = "pool.ntp.org", int timeoutMs = 3000, int maxRetries = 2);

    /**
     * @brief 判断当前系统时间是否可靠
     * @return true 可靠（曾经同步成功且距离上次同步不超过 2 小时）
     */
    bool isTimeReliable() const;

    /**
     * @brief 获取上次成功同步的时间点
     * @return 系统时钟时间点
     */
    std::chrono::system_clock::time_point getLastSyncTime() const;

private:
    void periodicSyncLoop(int intervalSec, int timeoutMs, int maxRetries);
    bool doSync(const std::string& server, int timeoutMs, int maxRetries, struct timeval& outTv);

    mutable std::mutex mutex_;
    std::thread worker_;
    bool running_;
    bool everSynced_;
    std::chrono::system_clock::time_point lastSyncTime_;
};

} // namespace ntp

#endif // NTP_TIME_SYNC_H