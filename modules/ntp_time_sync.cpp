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

namespace ntp {

// ========== 已有的 NTP 查询和同步函数（保持不变） ==========
struct NTPPacket {
    uint8_t  li_vn_mode;
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

static bool queryNtpServer(const std::string& server, int timeoutMs, struct timeval& outTv) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt");
        close(sock);
        return false;
    }

    struct hostent* host = gethostbyname(server.c_str());
    if (!host) {
        std::cerr << "Failed to resolve NTP server: " << server << std::endl;
        close(sock);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(123);
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    NTPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.li_vn_mode = 0x1B;   // LI=0, VN=3, Mode=3

    if (sendto(sock, &packet, sizeof(packet), 0,
               (struct sockaddr*)&addr, sizeof(addr)) != sizeof(packet)) {
        perror("sendto");
        close(sock);
        return false;
    }

    socklen_t addrLen = sizeof(addr);
    if (recvfrom(sock, &packet, sizeof(packet), 0,
                 (struct sockaddr*)&addr, &addrLen) != sizeof(packet)) {
        perror("recvfrom");
        close(sock);
        return false;
    }
    close(sock);

    uint32_t tx_secs = ntohl(packet.tx_ts_secs);
    uint32_t tx_frac = ntohl(packet.tx_ts_frac);
    const uint32_t NTP_EPOCH_DIFF = 2208988800U;

    if (tx_secs < NTP_EPOCH_DIFF) {
        std::cerr << "NTP timestamp too small: " << tx_secs << std::endl;
        return false;
    }

    outTv.tv_sec = (time_t)(tx_secs - NTP_EPOCH_DIFF);
    outTv.tv_usec = (suseconds_t)((double)tx_frac * 1000000.0 / (1LL << 32));

    // 合理性检查：年份应在 2020 年至 2100 年之间
    if (outTv.tv_sec < 1577836800U || outTv.tv_sec > 4102444800U) {
        std::cerr << "NTP time out of reasonable range: " << outTv.tv_sec << std::endl;
        return false;
    }
    return true;
}

bool syncSystemTime(const std::string& server, int timeoutMs, int retryCount) {
    struct timeval ntpTime;
    bool success = false;

    for (int i = 0; i <= retryCount; ++i) {
        if (queryNtpServer(server, timeoutMs, ntpTime)) {
            success = true;
            break;
        }
        std::cerr << "NTP query failed, retry " << i+1 << "/" << retryCount << std::endl;
    }

    if (!success) {
        std::cerr << "Failed to get time from NTP server after retries." << std::endl;
        return false;
    }

    std::cout << "NTP time: sec=" << ntpTime.tv_sec 
              << ", usec=" << ntpTime.tv_usec 
              << ", datetime=" << ctime(&ntpTime.tv_sec);

    if (settimeofday(&ntpTime, nullptr) != 0) {
        perror("settimeofday");
        return false;
    }

    std::cout << "System time synchronized successfully." << std::endl;
    return true;
}

// ========== NtpSync 类的实现 ==========
NtpSync::NtpSync() : running_(false), everSynced_(false) {}

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

bool NtpSync::syncNow() {
    // 使用已有的 syncSystemTime 函数，固定使用 pool.ntp.org，超时3000ms，重试2次
    bool ok = syncSystemTime("pool.ntp.org", 3000, 2);
    if (ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastSyncTime_ = std::chrono::system_clock::now();
        everSynced_ = true;
    }
    return ok;
}

bool NtpSync::isTimeReliable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!everSynced_) return false;
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastSyncTime_).count();
    // 如果距离上次同步超过2小时，认为不可靠
    return elapsed < 7200;
}

std::chrono::system_clock::time_point NtpSync::getLastSyncTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSyncTime_;
}

void NtpSync::periodicSyncLoop(int intervalSec, int timeoutMs, int maxRetries) {
    while (running_) {
        syncNow();  // 每次同步会更新 lastSyncTime_ 和 everSynced_
        // 等待 intervalSec 秒，但可被 stop 中断
        for (int i = 0; i < intervalSec && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

} // namespace ntp