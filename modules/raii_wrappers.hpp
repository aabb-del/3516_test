
// RAII包装类头文件 raii_wrappers.h
#ifndef RAII_WRAPPERS_H
#define RAII_WRAPPERS_H

#include "hi_comm_vi.h"
#include "hi_comm_vo.h"
#include "mpi_vi.h"
#include "mpi_vo.h"
#include <functional>
#include <memory>
#include <iostream>

// VI帧的RAII包装
class VIFrameRAII {
public:
    VIFrameRAII(int pipe, int chn) 
        : pipe_(pipe), chn_(chn), frame_(new VIDEO_FRAME_INFO_S{}), acquired_(false) {}

    ~VIFrameRAII() {
        if (acquired_ && frame_) {
            HI_MPI_VI_ReleaseChnFrame(pipe_, chn_, frame_);
        }
        delete frame_;
    }

    // 移动构造函数
    VIFrameRAII(VIFrameRAII&& other) noexcept
        : pipe_(other.pipe_), chn_(other.chn_), frame_(other.frame_), acquired_(other.acquired_) {
        other.frame_ = nullptr;
        other.acquired_ = false;
    }

    // 禁用拷贝
    VIFrameRAII(const VIFrameRAII&) = delete;
    VIFrameRAII& operator=(const VIFrameRAII&) = delete;

    bool acquire(int timeout_ms = 1000) {
        if (!frame_) return false;
        HI_S32 ret = HI_MPI_VI_GetChnFrame(pipe_, chn_, frame_, timeout_ms);
        acquired_ = (ret == HI_SUCCESS);
        return acquired_;
    }

    VIDEO_FRAME_INFO_S* get() { return frame_; }
    VIDEO_FRAME_INFO_S* operator->() { return frame_; }

private:
    int pipe_;
    int chn_;
    VIDEO_FRAME_INFO_S* frame_;
    bool acquired_;  // 新增标志
};



// MPP系统的RAII包装
class MPPSystemRAII {
public:
    MPPSystemRAII() {
        // 系统初始化代码
    }
    
    ~MPPSystemRAII() {
        // 系统去初始化代码
    }
    
    // 禁止拷贝和移动
    MPPSystemRAII(const MPPSystemRAII&) = delete;
    MPPSystemRAII& operator=(const MPPSystemRAII&) = delete;
    MPPSystemRAII(MPPSystemRAII&&) = delete;
    MPPSystemRAII& operator=(MPPSystemRAII&&) = delete;
};


// VO通道的RAII包装
class VOChnRAII {
public:
    VOChnRAII(int dev, int chn) : dev_(dev), chn_(chn) {}
    
    ~VOChnRAII() {
        // 可以在这里添加VO通道的清理代码
    }
    
    bool sendFrame(VIDEO_FRAME_INFO_S* frame, int timeout_ms = -1) {
        return HI_MPI_VO_SendFrame(dev_, chn_, frame, timeout_ms) == HI_SUCCESS;
    }
    
private:
    int dev_;
    int chn_;
};

#endif // RAII_WRAPPERS_H