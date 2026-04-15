#ifndef NTP_TIME_SYNC_H
#define NTP_TIME_SYNC_H

#include <string>

namespace ntp {

// 默认 NTP 服务器地址（可更换为更可靠的服务器）
const std::string DEFAULT_NTP_SERVER = "pool.ntp.org";
const int DEFAULT_TIMEOUT_MS = 5000;   // 请求超时 5 秒
const int DEFAULT_RETRY_COUNT = 2;     // 失败重试次数

/**
 * 从 NTP 服务器获取网络时间并更新系统时间
 * @param server   NTP 服务器域名或 IP，默认为 pool.ntp.org
 * @param timeoutMs  请求超时时间（毫秒）
 * @param retryCount 失败重试次数
 * @return true 表示成功，false 表示失败
 */
bool syncSystemTime(const std::string& server = DEFAULT_NTP_SERVER,
                    int timeoutMs = DEFAULT_TIMEOUT_MS,
                    int retryCount = DEFAULT_RETRY_COUNT);

} // namespace ntp

#endif // NTP_TIME_SYNC_H