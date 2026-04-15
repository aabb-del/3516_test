#include "venc_stream_guard.h"
#include <cstdlib>
#include <cstdio>

namespace hisi {
namespace venc {

// ==================== VENCStreamRAII ====================
VENCStreamRAII::VENCStreamRAII()
    : chn_(-1), pstPack_(nullptr), u32PackCount_(0), acquired_(false), isKeyFrame_(HI_FALSE) {
    memset(&stStream_, 0, sizeof(stStream_));
}

VENCStreamRAII::~VENCStreamRAII() {
    release();
}

void VENCStreamRAII::release() {
    if (acquired_ && chn_ >= 0) {
        HI_MPI_VENC_ReleaseStream(chn_, &stStream_);
    }
    if (pstPack_) {
        free(pstPack_);
        pstPack_ = nullptr;
    }
    u32PackCount_ = 0;
    acquired_ = false;
    isKeyFrame_ = HI_FALSE;
    chn_ = -1;
}

bool VENCStreamRAII::acquire(VENC_CHN chn, HI_S32 timeoutMs) {
    release();
    chn_ = chn;

    // 等待数据
    HI_S32 vencFd = HI_MPI_VENC_GetFd(chn);
    if (vencFd < 0) return false;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(vencFd, &read_fds);
    struct timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    HI_S32 ret = select(vencFd + 1, &read_fds, NULL, NULL, &tv);
    if (ret <= 0) return false;

    // 查询状态
    VENC_CHN_STATUS_S stStat;
    ret = HI_MPI_VENC_QueryStatus(chn, &stStat);
    if (ret != HI_SUCCESS || stStat.u32CurPacks == 0) return false;

    u32PackCount_ = stStat.u32CurPacks;
    pstPack_ = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * u32PackCount_);
    if (!pstPack_) return false;

    stStream_.pstPack = pstPack_;
    stStream_.u32PackCount = u32PackCount_;

    ret = HI_MPI_VENC_GetStream(chn, &stStream_, HI_TRUE);
    if (ret != HI_SUCCESS) {
        free(pstPack_);
        pstPack_ = nullptr;
        return false;
    }

    // 根据 stH264Info.enRefType 判断是否为关键帧（IDR）
    if (stStream_.stH264Info.enRefType == BASE_IDRSLICE) {
        isKeyFrame_ = HI_TRUE;
    } else {
        isKeyFrame_ = HI_FALSE;
    }

    acquired_ = true;
    return true;
}

const HI_U8* VENCStreamRAII::getPackData(HI_U32 idx) const {
    if (idx >= u32PackCount_) return nullptr;
    return pstPack_[idx].pu8Addr + pstPack_[idx].u32Offset;
}

HI_U32 VENCStreamRAII::getPackLen(HI_U32 idx) const {
    if (idx >= u32PackCount_) return 0;
    return pstPack_[idx].u32Len - pstPack_[idx].u32Offset;
}

bool VENCStreamRAII::isKeyFrame() const {
    return (isKeyFrame_ == HI_TRUE);
}

// 移动语义实现（简单示例）
VENCStreamRAII::VENCStreamRAII(VENCStreamRAII&& other) noexcept
    : chn_(other.chn_), stStream_(other.stStream_), pstPack_(other.pstPack_),
      u32PackCount_(other.u32PackCount_), acquired_(other.acquired_), isKeyFrame_(other.isKeyFrame_) {
    other.chn_ = -1;
    other.pstPack_ = nullptr;
    other.acquired_ = false;
}

VENCStreamRAII& VENCStreamRAII::operator=(VENCStreamRAII&& other) noexcept {
    if (this != &other) {
        release();
        chn_ = other.chn_;
        stStream_ = other.stStream_;
        pstPack_ = other.pstPack_;
        u32PackCount_ = other.u32PackCount_;
        acquired_ = other.acquired_;
        isKeyFrame_ = other.isKeyFrame_;
        other.chn_ = -1;
        other.pstPack_ = nullptr;
        other.acquired_ = false;
    }
    return *this;
}

// ==================== VENCChannelGuard ====================
static HI_S32 CreateVencChannel(VENC_CHN chn, const VENCConfig& cfg) {
    HI_U32 width = 1920, height = 1080;  // 实际应根据 cfg.enSize 转换
    VENC_CHN_ATTR_S stAttr;
    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.stVencAttr.enType = cfg.enType;
    stAttr.stVencAttr.u32MaxPicWidth = width;
    stAttr.stVencAttr.u32MaxPicHeight = height;
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32BufSize = width * height * 2;
    stAttr.stVencAttr.u32Profile = cfg.u32Profile;
    stAttr.stVencAttr.bByFrame = cfg.bByFrame;

    stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    VENC_H264_CBR_S* pstCbr = &stAttr.stRcAttr.stH264Cbr;
    pstCbr->u32Gop = cfg.u32Gop;
    pstCbr->u32StatTime = 1;
    pstCbr->u32SrcFrameRate = cfg.u32SrcFrameRate;
    pstCbr->fr32DstFrameRate = cfg.u32DstFrameRate;
    pstCbr->u32BitRate = cfg.u32BitRate;

    stAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    stAttr.stGopAttr.stNormalP.s32IPQpDelta = 2;

    return HI_MPI_VENC_CreateChn(chn, &stAttr);
}

VENCChannelGuard::VENCChannelGuard(VENC_CHN chn, const VENCConfig& cfg)
    : chn_(chn), valid_(false), vencFd_(-1) {
    if (CreateVencChannel(chn_, cfg) != HI_SUCCESS) return;
    VENC_RECV_PIC_PARAM_S stRecvParam = { -1 };
    if (HI_MPI_VENC_StartRecvFrame(chn_, &stRecvParam) != HI_SUCCESS) {
        HI_MPI_VENC_DestroyChn(chn_);
        return;
    }
    vencFd_ = HI_MPI_VENC_GetFd(chn_);
    if (vencFd_ < 0) {
        HI_MPI_VENC_StopRecvFrame(chn_);
        HI_MPI_VENC_DestroyChn(chn_);
        return;
    }
    valid_ = true;
}

VENCChannelGuard::~VENCChannelGuard() {
    if (valid_) {
        HI_MPI_VENC_StopRecvFrame(chn_);
        HI_MPI_VENC_DestroyChn(chn_);
    }
}

VENCStreamRAII VENCChannelGuard::getFrame(HI_S32 timeoutMs) {
    VENCStreamRAII stream;
    if (valid_) stream.acquire(chn_, timeoutMs);
    return stream;
}

// 移动构造等可参考 VIFrameGuard，此处省略

} // namespace venc
} // namespace hisi