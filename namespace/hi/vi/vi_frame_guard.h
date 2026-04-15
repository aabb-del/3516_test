#ifndef VI_FRAME_GUARD_H
#define VI_FRAME_GUARD_H

#include <cstdint>
#include "hi_comm_video.h"
#include "hi_comm_vi.h"

namespace hisi {
namespace vi {

// RAII 包装器：管理从 VI 通道获取的一帧视频数据，自动释放资源
// 使用方法：
//   VIFrameGuard frame(pipe, chn);
//   if (frame.acquire(1000)) {
//       // 使用 frame->... 访问 VIDEO_FRAME_INFO_S
//   } // 离开作用域自动释放
class VIFrameGuard {
public:
    VIFrameGuard(int pipe, int chn);
    ~VIFrameGuard();

    // 禁止拷贝
    VIFrameGuard(const VIFrameGuard&) = delete;
    VIFrameGuard& operator=(const VIFrameGuard&) = delete;

    // 支持移动语义
    VIFrameGuard(VIFrameGuard&& other) noexcept;
    VIFrameGuard& operator=(VIFrameGuard&& other) noexcept;

    // 获取一帧（阻塞等待，超时单位毫秒）
    bool acquire(int timeoutMs = 1000);

    // 访问内部 VIDEO_FRAME_INFO_S 结构体
    VIDEO_FRAME_INFO_S* get() { return frame_; }
    const VIDEO_FRAME_INFO_S* get() const { return frame_; }
    VIDEO_FRAME_INFO_S* operator->() { return frame_; }
    const VIDEO_FRAME_INFO_S* operator->() const { return frame_; }

    // 是否持有有效帧
    bool isValid() const { return acquired_; }

    // 显式释放（可选，通常由析构自动完成）
    void release();

private:
    int pipe_;
    int chn_;
    VIDEO_FRAME_INFO_S* frame_;
    bool acquired_;   // 标记是否成功获取到帧，仅当 true 时才调用 ReleaseChnFrame
};

} // namespace vi
} // namespace hisi

#endif // VI_FRAME_GUARD_H