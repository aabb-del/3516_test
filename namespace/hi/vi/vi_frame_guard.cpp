#include "vi_frame_guard.h"
#include <cstring>
#include <cstdio>

#include "mpi_vi.h"

namespace hisi {
namespace vi {

VIFrameGuard::VIFrameGuard(int pipe, int chn)
    : pipe_(pipe), chn_(chn), frame_(new VIDEO_FRAME_INFO_S), acquired_(false) {
    std::memset(frame_, 0, sizeof(VIDEO_FRAME_INFO_S));
}

VIFrameGuard::~VIFrameGuard() {
    release();
    delete frame_;
}

void VIFrameGuard::release() {
    if (acquired_ && frame_) {
        HI_MPI_VI_ReleaseChnFrame(pipe_, chn_, frame_);
        acquired_ = false;
    }
    // 注意：不清空 frame_ 指针，因为析构时会 delete
}

bool VIFrameGuard::acquire(int timeoutMs) {
    release();  // 如果之前持有帧，先释放
    if (!frame_) return false;

    HI_S32 ret = HI_MPI_VI_GetChnFrame(pipe_, chn_, frame_, timeoutMs);
    acquired_ = (ret == HI_SUCCESS);
    return acquired_;
}

// 移动构造函数
VIFrameGuard::VIFrameGuard(VIFrameGuard&& other) noexcept
    : pipe_(other.pipe_), chn_(other.chn_), frame_(other.frame_), acquired_(other.acquired_) {
    other.frame_ = nullptr;
    other.acquired_ = false;
}

// 移动赋值运算符
VIFrameGuard& VIFrameGuard::operator=(VIFrameGuard&& other) noexcept {
    if (this != &other) {
        release();
        delete frame_;

        pipe_ = other.pipe_;
        chn_ = other.chn_;
        frame_ = other.frame_;
        acquired_ = other.acquired_;

        other.frame_ = nullptr;
        other.acquired_ = false;
    }
    return *this;
}

} // namespace vi
} // namespace hisi