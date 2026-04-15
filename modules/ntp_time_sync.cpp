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

// NTP 消息结构（网络字节序）
struct NTPPacket {
    uint8_t  li_vn_mode;    // Leap Indicator, Version, Mode
    uint8_t  stratum;       // Stratum level
    uint8_t  poll;          // Poll interval
    uint8_t  precision;     // Precision
    uint32_t root_delay;    // Root delay
    uint32_t root_disp;     // Root dispersion
    uint32_t ref_id;        // Reference ID
    uint32_t ref_ts_secs;   // Reference timestamp seconds
    uint32_t ref_ts_frac;   // Reference timestamp fraction
    uint32_t orig_ts_secs;  // Origin timestamp seconds
    uint32_t orig_ts_frac;  // Origin timestamp fraction
    uint32_t recv_ts_secs;  // Receive timestamp seconds
    uint32_t recv_ts_frac;  // Receive timestamp fraction
    uint32_t tx_ts_secs;    // Transmit timestamp seconds
    uint32_t tx_ts_frac;    // Transmit timestamp fraction
};

// 发送 NTP 请求并接收响应
static bool queryNtpServer(const std::string& server, int timeoutMs, struct timeval& outTv) {
    // 1. 创建 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }

    // 2. 设置接收超时
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt");
        close(sock);
        return false;
    }

    // 3. 解析服务器地址
    struct hostent* host = gethostbyname(server.c_str());
    if (!host) {
        std::cerr << "Failed to resolve NTP server: " << server << std::endl;
        close(sock);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(123);  // NTP 端口
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    // 4. 构建 NTP 请求包
    NTPPacket packet;
    memset(&packet, 0, sizeof(packet));
    // LI = 0 (no warning), VN = 3 (NTP version 3), Mode = 3 (client)
    packet.li_vn_mode = 0x1B;   // 00 011 011 = 0x1B

    // 5. 发送请求
    if (sendto(sock, &packet, sizeof(packet), 0,
               (struct sockaddr*)&addr, sizeof(addr)) != sizeof(packet)) {
        perror("sendto");
        close(sock);
        return false;
    }

    // 6. 接收响应
    socklen_t addrLen = sizeof(addr);
    if (recvfrom(sock, &packet, sizeof(packet), 0,
                 (struct sockaddr*)&addr, &addrLen) != sizeof(packet)) {
        perror("recvfrom");
        close(sock);
        return false;
    }

    close(sock);

    // 7. 解析服务器发送时间戳（tx_ts_secs, tx_ts_frac），并转换为主机字节序
    uint32_t tx_secs = ntohl(packet.tx_ts_secs);
    uint32_t tx_frac = ntohl(packet.tx_ts_frac);

    // NTP 纪元 (1900-01-01) 到 Unix 纪元 (1970-01-01) 的秒数差
    const uint32_t NTP_EPOCH_DIFF = 2208988800U;

    // 检查是否过小或过大（避免无效时间）
    if (tx_secs < NTP_EPOCH_DIFF) {
        std::cerr << "NTP timestamp too small: " << tx_secs << std::endl;
        return false;
    }

    // 转换为 Unix 时间
    outTv.tv_sec = (time_t)(tx_secs - NTP_EPOCH_DIFF);
    outTv.tv_usec = (suseconds_t)((double)tx_frac * 1000000.0 / (1LL << 32));

    // 合理性检查：年份应在 2020 年至 2100 年之间（粗略）
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

    // 打印获取到的时间（用于调试）
    std::cout << "NTP time: sec=" << ntpTime.tv_sec 
              << ", usec=" << ntpTime.tv_usec 
              << ", datetime=" << ctime(&ntpTime.tv_sec);

    // 设置系统时间（需要 root 权限）
    if (settimeofday(&ntpTime, nullptr) != 0) {
        perror("settimeofday");
        return false;
    }

    // 可选：同时设置硬件时钟（RTC）
    // system("hwclock -w");

    std::cout << "System time synchronized successfully." << std::endl;
    return true;
}

} // namespace ntp