#include <thread>
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#include "vi_frame_guard.h"
#include "venc_stream_guard.h"
#include "rtsp_publisher.h"
#include "sample_comm.h"
#include "mpp.hpp"
#include "ntp_time_sync.h"      // 增强版
#include "file_recorder.h"      // 新模块
#include "overlay_region_manager.h"
#include "osd_font.h"
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

std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&now_c, &tm_buf);  // 线程安全版本
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &tm_buf);
    return std::string(buffer);
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
    std::vector<hisi::region::RegionId> osdIds;   // 存储创建的OSD ID，便于后续管理

    // 获取管理器单例
    auto& mgr = hisi::region::OverlayRegionManager::getInstance();



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


        // 构建VENC通道的MPP_CHN_S结构
        MPP_CHN_S vencChn;
        vencChn.enModId = HI_ID_VENC;
        vencChn.s32DevId = 0;
        vencChn.s32ChnId = cfg.vencChn;    // 通道号

        // 设置OSD显示属性（位置：右下角，假设1080P，logo尺寸320x240）
        hisi::region::RegionDisplayAttr osdAttr;
        osdAttr.x = 10;   // 屏幕宽度 - logo宽度
        osdAttr.y = 10;   // 屏幕高度 - logo高度
        osdAttr.layer = 1;        // 图层号
        osdAttr.fgAlpha = 128;    // 完全不透明
        osdAttr.bgAlpha = 0;      // 背景完全透明

        // 创建OVERLAY区域（自动从BMP读取尺寸）
        hisi::region::RegionId osdId = mgr.createOverlay(
            vencChn,                           // 目标通道
            hisi::region::RegionType::OVERLAY, // 普通OVERLAY
            "./res/logo.bmp",                  // BMP文件路径（需存在）
            PIXEL_FORMAT_ARGB_1555,            // 像素格式
            osdAttr,
            0x00000000                         // 背景色（透明）
        );

        if (osdId != 0) {
            std::cout << "OSD created for VENC channel " << cfg.vencChn << ", ID=" << osdId << std::endl;
            osdIds.push_back(osdId);
        } else {
            std::cerr << "Failed to create OSD for channel " << cfg.vencChn << std::endl;
        }

        hisi::region::RegionDisplayAttr dispAttr;
        dispAttr.x = 80;
        dispAttr.y = 10;
        dispAttr.layer = 1;
        dispAttr.fgAlpha = 128;   // 半透明（海思范围 0～128）
        dispAttr.bgAlpha = 0;

        // 创建一个 Overlay 区域，尺寸可以稍微比文本宽高大一些
        hisi::region::RegionId textOsdId = mgr.createOverlay(
            vencChn,
            hisi::region::RegionType::OVERLAY,
            800, 100,                     // 宽高（像素），可根据文本估算
            "",                           // 不需要 BMP 文件
            PIXEL_FORMAT_ARGB_1555,
            dispAttr
        );

        if (textOsdId != 0) {
            std::cout << "OSD created for VENC channel " << cfg.vencChn << ", ID=" << textOsdId << std::endl;
            osdIds.push_back(textOsdId);
        } else {
            std::cerr << "Failed to create OSD for channel " << cfg.vencChn << std::endl;
        }

        std::thread updateThread([textOsdId, chn = cfg.vencChn, &mgr] {
            // 创建字体（如果系统中没有 "SourceHanSerifCN"，会自动 Fallback 到 "Builtin" 并打印警告）
            hisi::osd::OsdFont font("SourceHanSerifCN-Regular-1", 48,
                                    hisi::osd::FontStyleNormal,
                                    hisi::osd::PixelFormat::ARGB1555,
                                    0xFF000000);
            std::string extra_info="";
            if(chn == 0)    extra_info="可见光";
            else            extra_info="红外";


            while (g_running) {
                std::string timeStr = getCurrentTimeString() + " " + extra_info;
                mgr.updateOverlayBitMap(textOsdId, [&](void* canvas, uint32_t stride, int w, int h) {
                    memset(canvas, 0, stride * h);
                    hisi::osd::OsdPainter::drawText(canvas, stride, w, h, timeStr, 0, 10 + font.ascent(), font, w-20, "□");
                });

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        threads.push_back(std::move(updateThread));






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