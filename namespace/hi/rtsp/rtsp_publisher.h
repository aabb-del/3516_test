#ifndef RTSP_PUBLISHER_H
#define RTSP_PUBLISHER_H

#include <memory>
#include <string>
#include <cstdint>



namespace hisi {
namespace rtsp {

// RTSP 发布器配置
struct RTSPConfig {
    std::string ip = "0.0.0.0";
    uint16_t port = 554;
    std::string suffix = "live";       // rtsp://ip:port/suffix
    bool enableAuth = false;
    std::string realm = "HisiIPC";
    std::string username = "admin";
    std::string password = "12345";
};

// RAII 管理 RTSP 服务器，自动启动/停止
class RTSPPublisher {
public:
    explicit RTSPPublisher(const RTSPConfig& cfg);
    ~RTSPPublisher();

    // 禁止拷贝，允许移动
    RTSPPublisher(const RTSPPublisher&) = delete;
    RTSPPublisher& operator=(const RTSPPublisher&) = delete;
    RTSPPublisher(RTSPPublisher&& other) noexcept;
    RTSPPublisher& operator=(RTSPPublisher&& other) noexcept;

    // 推送 H.264 帧（I/P 帧）
    // data: 编码数据（不含 start code，xop 内部会添加）
    // size: 数据大小
    // isKeyFrame: 是否为关键帧（I帧）
    // timestamp: 时间戳（微秒，0 表示自动生成）
    bool pushH264Frame(const uint8_t* data, size_t size, bool isKeyFrame, uint64_t timestamp = 0);

    // 推送 H.265 帧（若需要）
    bool pushH265Frame(const uint8_t* data, size_t size, bool isKeyFrame, uint64_t timestamp = 0);

    // 获取当前客户端数量（可选）
    int getClientCount() const;

    // 服务器是否正常运行
    bool isValid() const { return valid_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool valid_;
};

} // namespace rtsp
} // namespace hisi

#endif // RTSP_PUBLISHER_H