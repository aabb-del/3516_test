#include <thread>
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <vector>
#include "vi_frame_guard.h"
#include "venc_stream_guard.h"
#include "rtsp_publisher.h"
#include "sample_comm.h"
#include "mpp.hpp"
#include "ntp_time_sync.h"

static std::atomic<bool> g_running(true);

void signalHandler(int) {
    g_running = false;
}


void viToVencThread(int pipeId, int viChn, std::shared_ptr<hisi::venc::VENCChannelGuard> vencGuard) {
    hisi::vi::VIFrameGuard viFrame(pipeId, viChn);
    int vencChn = vencGuard->getChn();

    while (g_running) {
        // 获取 VI 帧
        if (!viFrame.acquire(1000)) {
            continue;
        }
        // 送入编码器
        HI_S32 ret = HI_MPI_VENC_SendFrame(vencChn, viFrame.get(), 1000);
        if (ret != HI_SUCCESS) {
            std::cerr << "SendFrame to VENC chn " << vencChn << " failed, ret=0x" 
                      << std::hex << ret << std::endl;
        }
        // VI 帧会在下一次 acquire 时自动释放
    }
}



void vencToRtspThread(std::shared_ptr<hisi::venc::VENCChannelGuard> vencGuard,
                      int rtspPort, const std::string& rtspSuffix) {
    int vencChn = vencGuard->getChn();

    // 启动 RTSP 服务器
    hisi::rtsp::RTSPConfig rtspCfg;
    rtspCfg.ip = "0.0.0.0";
    rtspCfg.port = rtspPort;
    rtspCfg.suffix = rtspSuffix;
    hisi::rtsp::RTSPPublisher rtsp(rtspCfg);
    if (!rtsp.isValid()) {
        std::cerr << "Failed to start RTSP server on port " << rtspPort << std::endl;
        return;
    }



    while (g_running) {
        // 获取一帧编码流
        hisi::venc::VENCStreamRAII stream;
        if (stream.acquire(vencChn, 2000)) {   // 超时2秒
            bool isKeyFrame = stream.isKeyFrame();   // 根据 stH264Info.enRefType 判断 IDR 帧
            for (uint32_t i = 0; i < stream.getPackCount(); ++i) {
                const uint8_t* data = stream.getPackData(i);
                size_t len = stream.getPackLen(i);
                if (data && len > 0) {
                    rtsp.pushH264Frame(data, len, isKeyFrame);
                }
            }
        } else {
            // 超时或错误，继续循环
        }
    }
}




int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 时间同步
    if (ntp::syncSystemTime("pool.ntp.org", 3000, 2)) {
        std::cout << "Time sync success." << std::endl;
    } else {
        std::cerr << "Time sync failed." << std::endl;
    }


    // 初始化海思 MPP 系统、VI 等（省略）
    Mpp mpp;
    mpp.vi_init();
    // mpp.vo_init();

    // 定义通道配置
    struct ChannelConfig {
        int pipeId;
        int viChn;
        int vencChn;
        int rtspPort;
        std::string suffix;
    };

    std::vector<ChannelConfig> channels = {
        {0, 0, 0, 554, "cam0"},
        {1, 0, 1, 555, "cam1"}
    };

    std::vector<std::thread> threads;

    for (const auto& cfg : channels) {
        // 创建 VENC 通道（使用 RAII 管理生命周期）
        hisi::venc::VENCConfig vencCfg;
        vencCfg.enType = PT_H264;
        vencCfg.enSize = PIC_1080P;   // 请根据实际 VI 输出尺寸设置
        vencCfg.u32BitRate = 2048;
        vencCfg.u32SrcFrameRate = 30;
        vencCfg.u32DstFrameRate = 30;

        auto vencGuard = std::make_shared<hisi::venc::VENCChannelGuard>(cfg.vencChn, vencCfg);
        if (!vencGuard->isValid()) {
            std::cerr << "Failed to create VENC channel " << cfg.vencChn << std::endl;
            continue;
        }

        // 启动生产者线程（VI → VENC）
        threads.emplace_back(viToVencThread, cfg.pipeId, cfg.viChn, vencGuard);
        // 启动消费者线程（VENC → RTSP）
        threads.emplace_back(vencToRtspThread, vencGuard, cfg.rtspPort, cfg.suffix);
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    return 0;
}







