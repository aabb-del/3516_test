#ifndef VENC_STREAM_GUARD_H
#define VENC_STREAM_GUARD_H

#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/select.h>
#include "sample_comm.h"
#include "hi_comm_venc.h"
#include "mpi_venc.h"

namespace hisi {
namespace venc {

// 编码通道配置
struct VENCConfig {
    PAYLOAD_TYPE_E enType = PT_H264;
    PIC_SIZE_E     enSize = PIC_1080P;
    HI_U32         u32Profile = 0;
    HI_U32         u32Gop = 30;
    HI_U32         u32BitRate = 2048;   // kbps
    HI_U32         u32SrcFrameRate = 30;
    HI_U32         u32DstFrameRate = 30;
    HI_BOOL        bByFrame = HI_TRUE;
};

// RAII 管理一帧编码流
class VENCStreamRAII {
public:
    VENCStreamRAII();
    ~VENCStreamRAII();

    // 禁止拷贝，允许移动
    VENCStreamRAII(const VENCStreamRAII&) = delete;
    VENCStreamRAII& operator=(const VENCStreamRAII&) = delete;
    VENCStreamRAII(VENCStreamRAII&& other) noexcept;
    VENCStreamRAII& operator=(VENCStreamRAII&& other) noexcept;

    // 获取流（阻塞）
    bool acquire(VENC_CHN chn, HI_S32 timeoutMs = 1000);
    void release();

    bool isValid() const { return acquired_; }
    HI_U32 getPackCount() const { return u32PackCount_; }
    const HI_U8* getPackData(HI_U32 idx) const;
    HI_U32 getPackLen(HI_U32 idx) const;

    // 判断当前帧是否为关键帧（IDR）
    bool isKeyFrame() const;

    // 原始结构体（高级用法）
    VENC_STREAM_S* getStream() { return &stStream_; }

private:
    VENC_CHN        chn_;
    VENC_STREAM_S   stStream_;
    VENC_PACK_S*    pstPack_;
    HI_U32          u32PackCount_;
    bool            acquired_;
    HI_BOOL         isKeyFrame_;   // 缓存帧类型
};

// RAII 管理编码通道
class VENCChannelGuard {
public:
    VENCChannelGuard(VENC_CHN chn, const VENCConfig& cfg);
    ~VENCChannelGuard();

    VENCChannelGuard(const VENCChannelGuard&) = delete;
    VENCChannelGuard& operator=(const VENCChannelGuard&) = delete;
    VENCChannelGuard(VENCChannelGuard&& other) noexcept;
    VENCChannelGuard& operator=(VENCChannelGuard&& other) noexcept;

    VENCStreamRAII getFrame(HI_S32 timeoutMs = 1000);
    VENC_CHN getChn() const { return chn_; }
    bool isValid() const { return valid_; }

private:
    VENC_CHN chn_;
    bool valid_;
    HI_S32 vencFd_;
};

} // namespace venc
} // namespace hisi

#endif