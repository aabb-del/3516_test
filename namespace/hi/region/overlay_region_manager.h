#ifndef HISI_REGION_OVERLAY_REGION_MANAGER_H
#define HISI_REGION_OVERLAY_REGION_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <functional>
#include "hi_common.h"
#include "hi_comm_region.h"
#include "mpi_region.h"


namespace hisi {
namespace region {

// 区域类型枚举
enum class RegionType {
    OVERLAY,      // OVERLAY_RGN
    OVERLAY_EX,   // OVERLAYEX_RGN
    COVER,        // COVER_RGN
    COVER_EX,     // COVEREX_RGN
    MOSAIC        // MOSAIC_RGN
};

// 通用显示属性（位置、层次、透明度等）
struct RegionDisplayAttr {
    HI_S32 x = 0;
    HI_S32 y = 0;
    HI_U32 layer = 0;
    HI_U32 fgAlpha = 255;   // 仅对 OVERLAY 系列有效
    HI_U32 bgAlpha = 0;     // 仅对 OVERLAY 系列有效
};

// COVER 区域专用属性
struct CoverAttr {
    HI_U32 width = 0;
    HI_U32 height = 0;
    HI_U32 color = 0xFF0000FF; // ARGB，例如 0xFF0000FF 为蓝色不透明
};

// MOSAIC 区域专用属性
struct MosaicAttr {
    HI_U32 width = 0;
    HI_U32 height = 0;
    MOSAIC_BLK_SIZE_E blkSize = MOSAIC_BLK_SIZE_16;
};

// 用户使用的区域 ID
using RegionId = HI_U32;
using RGN_HANDLE = HI_S32;


class OverlayRegionManager {
public:
    static OverlayRegionManager& getInstance();

    // 禁止拷贝和移动
    OverlayRegionManager(const OverlayRegionManager&) = delete;
    OverlayRegionManager& operator=(const OverlayRegionManager&) = delete;

    // ---------- 创建 OVERLAY / OVERLAYEX 区域（带位图）----------
    // 自动从 BMP 获取尺寸
    RegionId createOverlay(const MPP_CHN_S& chn,
                           RegionType type,  // RegionType::OVERLAY 或 OVERLAY_EX
                           const std::string& bmpFilePath,
                           PIXEL_FORMAT_E pixelFormat = PIXEL_FORMAT_ARGB_1555,
                           const RegionDisplayAttr& attr = RegionDisplayAttr(),
                           HI_U32 bgColor = 0x00000000);

    // 手动指定宽高（BMP 尺寸必须匹配）
    RegionId createOverlay(const MPP_CHN_S& chn,
                           RegionType type,
                           HI_U32 width, HI_U32 height,
                           const std::string& bmpFilePath,
                           PIXEL_FORMAT_E pixelFormat = PIXEL_FORMAT_ARGB_1555,
                           const RegionDisplayAttr& attr = RegionDisplayAttr(),
                           HI_U32 bgColor = 0x00000000);

    // ---------- 创建 COVER / COVEREX 区域 ----------
    RegionId createCover(const MPP_CHN_S& chn,
                         RegionType type,  // RegionType::COVER 或 COVER_EX
                         const CoverAttr& coverAttr,
                         const RegionDisplayAttr& attr = RegionDisplayAttr());

    // ---------- 创建 MOSAIC 区域 ----------
    RegionId createMosaic(const MPP_CHN_S& chn,
                          const MosaicAttr& mosaicAttr,
                          const RegionDisplayAttr& attr = RegionDisplayAttr());
    
    // ---------- 画布直接更新（适用于动态内容，如时间戳）----------
    // 参数：
    //   id         : 区域ID
    //   drawCallback: 回调函数，用户在此回调中直接向画布内存绘制内容
    //                 canvasAddr: 画布虚拟地址（用户可直接写）
    //                 stride    : 画布行跨度（字节）
    //                 width     : 画布宽度（像素）
    //                 height    : 画布高度（像素）
    // 返回：成功 true，失败 false
    bool updateOverlayCanvas(RegionId id, std::function<void(void* canvasAddr, HI_U32 stride, HI_U32 width, HI_U32 height)> drawCallback);
    bool updateOverlayBitMap(RegionId id, std::function<void(void* canvasAddr, HI_U32 stride, HI_U32 width, HI_U32 height)> drawCallback);
    // ---------- 通用操作 ----------
    bool setOverlayBitmapByData(RegionId id, 
                            const std::vector<HI_U8>& bitmapData,
                            PIXEL_FORMAT_E pixelFormat);
    bool updateOverlayBitmap(RegionId id, const std::string& bmpFilePath);
    bool setPosition(RegionId id, HI_S32 x, HI_S32 y);
    bool setAlpha(RegionId id, HI_U32 fgAlpha, HI_U32 bgAlpha = 0);
    bool destroyRegion(RegionId id);
    void destroyRegionsOnChn(const MPP_CHN_S& chn);
    bool isValid(RegionId id) const;
    bool cleanCanvas(RegionId id);

private:
    OverlayRegionManager() = default;
    ~OverlayRegionManager();

    // 内部区域信息
    struct RegionInfo {
        RGN_HANDLE handle = 0;
        MPP_CHN_S chn;
        RegionType type;
        bool attached = false;
        // 对于 OVERLAY 系列
        HI_U32 width = 0;
        HI_U32 height = 0;
        PIXEL_FORMAT_E pixelFormat = PIXEL_FORMAT_BUTT;
        // 对于 COVER
        CoverAttr coverAttr;
        // 对于 MOSAIC
        MosaicAttr mosaicAttr;
    };

    // 内部辅助函数
    bool createRegionObject(const RegionInfo& info, HI_U32 bgColor = 0);
    bool attachRegionToChn(RegionInfo& info, const RegionDisplayAttr& attr);
    bool loadBmpToRegion(RGN_HANDLE handle, const RegionInfo& info, const std::string& filePath);
    void detachAndDestroy(RGN_HANDLE handle, const MPP_CHN_S& chn);

    static bool getBmpSize(const std::string& filePath, HI_U32& width, HI_U32& height);
    static HI_U32 hashChn(const MPP_CHN_S& chn);

    std::atomic<HI_U32> next_handle_{100};
    std::atomic<RegionId> next_id_{1};

    std::unordered_map<RegionId, RegionInfo> regions_;
    std::unordered_map<HI_U32, std::vector<RegionId>> chnToIds_;
};

} // namespace region
} // namespace hisi

#endif