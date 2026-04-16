#include "ntp_time_sync.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <cmath>

namespace ntp {

// NTP 协议包结构
#pragma pack(push, 1)
struct NTPPacket {
    uint8_t  li_vn_mode;    // Leap Indicator, Version, Mode
    uint8_t  stratum;
    uint8_t  poll;
    uint8_t  precision;
    uint32_t root_delay;
    uint32_t root_disp;
    uint32_t ref_id;
    uint32_t ref_ts_secs;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_secs;
    uint32_t orig_ts_frac;
    uint32_t recv_ts_secs;
    uint32_t recv_ts_frac;
    uint32_t tx_ts_secs;
    uint32_t tx_ts_frac;
};
#pragma pack(pop)

// NTP 时间纪元偏移（1900年1月1日 到 1970年1月1日 的秒数）
static const uint32_t NTP_EPOCH_DIFF = 2208988800U;

/**
 * @brief 查询单个 NTP 服务器，获取服务器时间戳
 * @param server     域名或IP
 * @param timeoutMs  超时毫秒
 * @param outTv      输出时间戳（Unix timeval）
 * @return true 成功
 */
static bool queryNtpServer(const std::string& server, int timeoutMs, struct timeval& outTv) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "[NTP] socket() failed: " << strerror(errno) << std::endl;
        return false;
    }

    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "[NTP] setsockopt(SO_RCVTIMEO) failed: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }

    // 解析服务器地址
    struct hostent* host = gethostbyname(server.c_str());
    if (!host) {
        std::cerr << "[NTP] Failed to resolve server: " << server << std::endl;
        close(sock);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(123);
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    // 构造 NTP 请求包（客户端模式，版本3）
    NTPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.li_vn_mode = 0x1B;   // LI=0, VN=3, Mode=3

    // 发送请求
    if (sendto(sock, &packet, sizeof(packet), 0,
               (struct sockaddr*)&addr, sizeof(addr)) != sizeof(packet)) {
        std::cerr << "[NTP] sendto() failed: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }

    // 接收响应
    socklen_t addrLen = sizeof(addr);
    ssize_t recvLen = recvfrom(sock, &packet, sizeof(packet), 0,
                               (struct sockaddr*)&addr, &addrLen);
    if (recvLen != sizeof(packet)) {
        if (recvLen < 0) {
            std::cerr << "[NTP] recvfrom() failed: " << strerror(errno) << std::endl;
        } else {
            std::cerr << "[NTP] recvfrom() returned short packet: " << recvLen << std::endl;
        }
        close(sock);
        return false;
    }
    close(sock);

    // 提取发送时间戳（NTP 格式）
    uint32_t tx_secs = ntohl(packet.tx_ts_secs);
    uint32_t tx_frac = ntohl(packet.tx_ts_frac);

    // 检查秒数是否大于偏移量（防止负数）
    if (tx_secs < NTP_EPOCH_DIFF) {
        std::cerr << "[NTP] Invalid transmit timestamp (too small): " << tx_secs << std::endl;
        return false;
    }

    outTv.tv_sec = static_cast<time_t>(tx_secs - NTP_EPOCH_DIFF);
    // 将 NTP 分数部分（2^32 分之几）转换为微秒
    outTv.tv_usec = static_cast<suseconds_t>((static_cast<double>(tx_frac) / (1ULL << 32)) * 1000000.0);

    // 合理性检查：Unix 时间戳应在 2020-01-01 (1577836800) 到 2100-01-01 (4102444800) 之间
    if (outTv.tv_sec < 1577836800LL || outTv.tv_sec > 4102444800LL) {
        std::cerr << "[NTP] Timestamp out of reasonable range: " << outTv.tv_sec << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief 带指数退避重试的 NTP 查询
 * @param server      服务器地址
 * @param timeoutMs   单次超时
 * @param maxRetries  最大重试次数
 * @param outTv       成功时的时间戳
 * @return true 成功
 */
static bool queryNtpWithRetry(const std::string& server, int timeoutMs, int maxRetries, struct timeval& outTv) {
    for (int i = 0; i <= maxRetries; ++i) {
        if (queryNtpServer(server, timeoutMs, outTv)) {
            return true;
        }
        if (i < maxRetries) {
            // 指数退避：100ms, 200ms, 400ms, ...
            int backoffMs = 100 * (1 << i);
            if (backoffMs > 2000) backoffMs = 2000;
            std::cerr << "[NTP] Query failed, retry " << (i + 1) << "/" << maxRetries
                      << " after " << backoffMs << "ms" << std::endl;
            usleep(backoffMs * 1000);
        }
    }
    return false;
}

/**
 * @brief 显示 UTC 时间字符串（避免时区混淆）
 * @param tv  timeval 结构
 * @return   格式化字符串 "YYYY-MM-DD HH:MM:SS UTC"
 */
static std::string utcTimeString(const struct timeval& tv) {
    time_t sec = tv.tv_sec;
    struct tm tm;
    gmtime_r(&sec, &tm);  // 使用 UTC
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    char result[80];
    snprintf(result, sizeof(result), "%s.%03ld UTC", buf, tv.tv_usec / 1000);
    return std::string(result);
}

// ==================== NtpSync 类实现 ====================

NtpSync::NtpSync() : running_(false), everSynced_(false) {
}

NtpSync::~NtpSync() {
    stop();
}

bool NtpSync::startPeriodicSync(int intervalSec, int timeoutMs, int maxRetries) {
    if (running_) return true;
    running_ = true;
    worker_ = std::thread(&NtpSync::periodicSyncLoop, this, intervalSec, timeoutMs, maxRetries);
    return true;
}

void NtpSync::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool NtpSync::doSync(const std::string& server, int timeoutMs, int maxRetries, struct timeval& outTv) {
    if (!queryNtpWithRetry(server, timeoutMs, maxRetries, outTv)) {
        return false;
    }

    // 输出 UTC 时间，避免时区混淆
    std::cout << "[NTP] Server time: " << utcTimeString(outTv)
              << " (Unix sec=" << outTv.tv_sec << ", usec=" << outTv.tv_usec << ")" << std::endl;

    // 设置系统时间
    if (settimeofday(&outTv, nullptr) != 0) {
        std::cerr << "[NTP] settimeofday() failed: " << strerror(errno) << std::endl;
        return false;
    }

    std::cout << "[NTP] System time synchronized successfully." << std::endl;
    return true;
}

bool NtpSync::syncNow(const std::string& server, int timeoutMs, int maxRetries) {
    struct timeval ntpTime;
    if (!doSync(server, timeoutMs, maxRetries, ntpTime)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    lastSyncTime_ = std::chrono::system_clock::now();
    everSynced_ = true;
    return true;
}

bool NtpSync::isTimeReliable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!everSynced_) return false;
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastSyncTime_).count();
    // 距离上次同步超过 2 小时认为不可靠
    return elapsed < 7200;
}

std::chrono::system_clock::time_point NtpSync::getLastSyncTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSyncTime_;
}

void NtpSync::periodicSyncLoop(int intervalSec, int timeoutMs, int maxRetries) {
    while (running_) {
        struct timeval ntpTime;
        if (doSync("pool.ntp.org", timeoutMs, maxRetries, ntpTime)) {
            std::lock_guard<std::mutex> lock(mutex_);
            lastSyncTime_ = std::chrono::system_clock::now();
            everSynced_ = true;
        } else {
            std::cerr << "[NTP] Periodic sync failed, will retry after " << intervalSec << " seconds" << std::endl;
        }

        // 分段睡眠，以便快速响应 stop()
        for (int i = 0; i < intervalSec && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

} // namespace ntp