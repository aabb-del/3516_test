#include <thread>
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>
#include "vi_frame_guard.h"
#include "venc_stream_guard.h"
#include "rtsp_publisher.h"
#include "sample_comm.h"
#include "mpp.hpp"
#include "ntp_time_sync.h"      // 增强版
#include "file_recorder.h"      // 新模块
#include "pq.hpp"

static std::atomic<bool> g_running(true);

void signalHandler(int) {
    // 退出时停止控制服务
    hisi::pq::PQ::stopControlProcess();  // 停止 pq_control_main 线程
    g_running = false;
}

void viToVencThread(int pipeId, int viChn, std::shared_ptr<hisi::venc::VENCChannelGuard> vencGuard) {
    hisi::vi::VIFrameGuard viFrame(pipeId, viChn);
    int vencChn = vencGuard->getChn();
    while (g_running) {
        if (!viFrame.acquire(1000)) continue;
        HI_S32 ret = HI_MPI_VENC_SendFrame(vencChn, viFrame.get(), 1000);
        if (ret != HI_SUCCESS) {
            std::cerr << "SendFrame to VENC chn " << vencChn << " failed, ret=0x" 
                      << std::hex << ret << std::endl;
        }
    }
}

void vencToRtspAndFileThread(std::shared_ptr<hisi::venc::VENCChannelGuard> vencGuard,
                             int rtspPort, const std::string& rtspSuffix,
                             std::shared_ptr<hisi::storage::FileRecorder> recorder,
                             std::shared_ptr<ntp::NtpSync> ntpSync) {
    int vencChn = vencGuard->getChn();

    // RTSP 服务器
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
        hisi::venc::VENCStreamRAII stream;
        if (stream.acquire(vencChn, 2000)) {
            bool isKeyFrame = stream.isKeyFrame();
            for (uint32_t i = 0; i < stream.getPackCount(); ++i) {
                const uint8_t* data = stream.getPackData(i);
                size_t len = stream.getPackLen(i);
                if (data && len > 0) {
                    rtsp.pushH264Frame(data, len, isKeyFrame);
                    if (recorder) {
                        recorder->writeFrame(data, len, isKeyFrame);
                    }
                }
            }
        }
    }
}




int main(int argc, char **argv) {

    


    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 创建 NTP 周期同步实例并启动（每小时同步一次）
    auto ntpSync = std::make_shared<ntp::NtpSync>();
    ntpSync->syncNow();
    ntpSync->startPeriodicSync(3600, 3000, 2);



    // 初始化 MPP
    Mpp mpp;
    mpp.vi_init();



    // 通道配置
    struct ChannelConfig {
        int pipeId;
        int viChn;
        int vencChn;
        int rtspPort;
        std::string suffix;
        std::string recordDir;
        int durationSec;       // 每个文件录制时长（秒）
        int minFreeSpaceMB;    // 最小剩余空间（MB）
        size_t maxFileCount;   // 最大文件数量（0=不限制）
    };

    std::vector<ChannelConfig> channels = {
        {0, 0, 0, 554, "cam0", "/mnt/TF/record/cam0", 600*6, 1024, 0},   // 1小时一个文件，至少剩余1GB，不限制最多文件
        {1, 0, 1, 555, "cam1", "/mnt/TF/record/cam1", 600*6, 1024, 0}
    };

    std::vector<std::thread> threads;

    for (const auto& cfg : channels) {
        // VENC 配置
        hisi::venc::VENCConfig vencCfg;
        vencCfg.enType = PT_H264;
        vencCfg.enSize = PIC_1080P;
        vencCfg.u32BitRate = 8192;
        vencCfg.u32SrcFrameRate = 30;
        vencCfg.u32DstFrameRate = 30;

        auto vencGuard = std::make_shared<hisi::venc::VENCChannelGuard>(cfg.vencChn, vencCfg);
        if (!vencGuard->isValid()) {
            std::cerr << "Failed to create VENC channel " << cfg.vencChn << std::endl;
            continue;
        }

        // 创建文件录制器，传入时间可靠性回调（使用 NTP 同步状态）
        // 创建文件录制器时，传入时间可靠性回调
        auto recorder = std::make_shared<hisi::storage::FileRecorder>(
            cfg.recordDir, cfg.suffix, cfg.durationSec, cfg.minFreeSpaceMB, cfg.maxFileCount,
            [ntpSync]() { return ntpSync->isTimeReliable(); }
        );

        threads.emplace_back(viToVencThread, cfg.pipeId, cfg.viChn, vencGuard);
        threads.emplace_back(vencToRtspAndFileThread, vencGuard, cfg.rtspPort, cfg.suffix, recorder, ntpSync);
    }

    // 延时 1 秒
    usleep(1000000);

    hisi::pq::PQ pq;
    if (pq.loadFromFile("./binary_data_Hi3516CV500.bin")) {
        std::cout << "PQ parameters loaded." << std::endl;
    }

    hisi::pq::PQ::startControlProcess(argc, argv);

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }


    hisi::pq::PQ::stopControlProcess();  // 停止 pq_control_main 线程

    return 0;
}