#include "rtsp_publisher.h"
#include "xop/RtspServer.h"
#include "net/Timer.h"
#include <thread>
#include <atomic>



#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

static std::string getLocalIpAddress() {
    std::string ip;
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        return ip;
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, host, NI_MAXHOST);
            // 排除回环接口和 127.0.0.1
            if (strcmp(ifa->ifa_name, "lo") != 0 && strcmp(host, "127.0.0.1") != 0) {
                ip = host;
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return ip;
}




namespace hisi {
namespace rtsp {

class RTSPPublisher::Impl {
public:
    Impl(const RTSPConfig& cfg)
        : config_(cfg), valid_(false), clientCount_(0) {


        std::string bindIp = config_.ip;
        if (bindIp.empty() || bindIp == "0.0.0.0") {
            bindIp = getLocalIpAddress();
            if (bindIp.empty()) {
                printf("Failed to auto-detect IP address, fallback to 0.0.0.0\n");
                bindIp = "0.0.0.0";
            } else {
                printf("Auto-detected IP: %s\n", bindIp.c_str());
            }
        }
        std::cout << "RTSP: rtsp://" << bindIp.c_str() << ":" << config_.port << "/" << config_.suffix << std::endl;

        eventLoop_.reset(new xop::EventLoop());
        server_ = xop::RtspServer::Create(eventLoop_.get());
        if (!server_->Start(bindIp, config_.port)) {
            printf("RTSP server start failed on %s:%d\n", bindIp.c_str(), config_.port);
            return;
        }
        if (config_.enableAuth) {
            server_->SetAuthConfig(config_.realm, config_.username, config_.password);
        }

        // 创建媒体会话，只添加 H.264 源
        xop::MediaSession* session = xop::MediaSession::CreateNew(config_.suffix);
        session->AddSource(xop::channel_0, xop::H264Source::CreateNew());
        session->AddNotifyConnectedCallback([this](xop::MediaSessionId, std::string ip, uint16_t port) {
            clientCount_++;
            printf("RTSP client connect, ip=%s, port=%hu, total=%d\n", ip.c_str(), port, clientCount_.load());
        });
        session->AddNotifyDisconnectedCallback([this](xop::MediaSessionId, std::string ip, uint16_t port) {
            clientCount_--;
            printf("RTSP client disconnect, ip=%s, port=%hu, total=%d\n", ip.c_str(), port, clientCount_.load());
        });

        sessionId_ = server_->AddSession(session);
        valid_ = true;
    }

    ~Impl() {
        if (valid_) {
            // server_->RemoveMeidaSession(sessionId_);
        }
    }

    bool pushH264Frame(const uint8_t* data, size_t size, bool isKeyFrame, uint64_t timestamp) {
        if (!valid_ || clientCount_ == 0) return false; // 无客户端时不推流（可选）

        xop::AVFrame frame;
        frame.type = isKeyFrame ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;
        frame.size = size;
        frame.timestamp = (timestamp == 0) ? xop::H264Source::GetTimestamp() : timestamp;
        frame.buffer.reset(new uint8_t[size]);
        memcpy(frame.buffer.get(), data, size);
        return server_->PushFrame(sessionId_, xop::channel_0, frame);
    }

    bool pushH265Frame(const uint8_t* data, size_t size, bool isKeyFrame, uint64_t timestamp) {
        // 若需要支持 H.265，需添加 H265Source，此处略
        return false;
    }

    int getClientCount() const { return clientCount_.load(); }
    bool isValid() const { return valid_; }

private:
    RTSPConfig config_;
    std::shared_ptr<xop::EventLoop> eventLoop_;
    std::shared_ptr<xop::RtspServer> server_;
    xop::MediaSessionId sessionId_;
    std::atomic<int> clientCount_;
    bool valid_;
};

// 公有接口转发至 Impl
RTSPPublisher::RTSPPublisher(const RTSPConfig& cfg)
    : impl_(std::make_unique<Impl>(cfg)), valid_(impl_->isValid()) {}

RTSPPublisher::~RTSPPublisher() = default;

RTSPPublisher::RTSPPublisher(RTSPPublisher&& other) noexcept = default;
RTSPPublisher& RTSPPublisher::operator=(RTSPPublisher&& other) noexcept = default;

bool RTSPPublisher::pushH264Frame(const uint8_t* data, size_t size, bool isKeyFrame, uint64_t timestamp) {
    return impl_->pushH264Frame(data, size, isKeyFrame, timestamp);
}

bool RTSPPublisher::pushH265Frame(const uint8_t* data, size_t size, bool isKeyFrame, uint64_t timestamp) {
    return impl_->pushH265Frame(data, size, isKeyFrame, timestamp);
}

int RTSPPublisher::getClientCount() const {
    return impl_->getClientCount();
}

} // namespace rtsp
} // namespace hisi