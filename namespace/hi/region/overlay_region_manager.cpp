#include "overlay_region_manager.h"
#include "loadbmp.h"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace hisi {
namespace region {

// ==================== 静态辅助函数 ====================
bool OverlayRegionManager::getBmpSize(const std::string& filePath, HI_U32& width, HI_U32& height) {
    OSD_BITMAPFILEHEADER bmpFileHeader;
    OSD_BITMAPINFO bmpInfo;
    if (GetBmpInfo(filePath.c_str(), &bmpFileHeader, &bmpInfo) < 0) {
        return false;
    }
    width = bmpInfo.bmiHeader.biWidth;
    height = static_cast<HI_U32>(std::abs(bmpInfo.bmiHeader.biHeight));
    return true;
}

HI_U32 OverlayRegionManager::hashChn(const MPP_CHN_S& chn) {
    return (chn.enModId << 16) | (chn.s32DevId << 8) | chn.s32ChnId;
}

// ==================== 单例管理 ====================
OverlayRegionManager& OverlayRegionManager::getInstance() {
    static OverlayRegionManager instance;
    return instance;
}

OverlayRegionManager::~OverlayRegionManager() {
    // 销毁所有区域
    for (auto& pair : regions_) {
        const auto& info = pair.second;
        if (info.attached) {
            HI_MPI_RGN_DetachFromChn(info.handle, &info.chn);
        }
        HI_MPI_RGN_Destroy(info.handle);
    }
    regions_.clear();
    chnToIds_.clear();
}

// ==================== 内部创建函数 ====================
bool OverlayRegionManager::createRegionObject(const RegionInfo& info, HI_U32 bgColor) {
    RGN_ATTR_S stRegionAttr;
    memset(&stRegionAttr, 0, sizeof(stRegionAttr));

    switch (info.type) {
        case RegionType::OVERLAY:
            stRegionAttr.enType = OVERLAY_RGN;
            stRegionAttr.unAttr.stOverlay.enPixelFmt = info.pixelFormat;
            stRegionAttr.unAttr.stOverlay.stSize.u32Width = info.width;
            stRegionAttr.unAttr.stOverlay.stSize.u32Height = info.height;
            stRegionAttr.unAttr.stOverlay.u32BgColor = bgColor;
            stRegionAttr.unAttr.stOverlay.u32CanvasNum = 1;
            break;
        case RegionType::OVERLAY_EX:
            stRegionAttr.enType = OVERLAYEX_RGN;
            stRegionAttr.unAttr.stOverlayEx.enPixelFmt = info.pixelFormat;
            stRegionAttr.unAttr.stOverlayEx.stSize.u32Width = info.width;
            stRegionAttr.unAttr.stOverlayEx.stSize.u32Height = info.height;
            stRegionAttr.unAttr.stOverlayEx.u32BgColor = bgColor;
            stRegionAttr.unAttr.stOverlayEx.u32CanvasNum = 1;
            break;
        case RegionType::COVER:
            stRegionAttr.enType = COVER_RGN;
            break;
        case RegionType::COVER_EX:
            stRegionAttr.enType = COVEREX_RGN;
            break;
        case RegionType::MOSAIC:
            stRegionAttr.enType = MOSAIC_RGN;
            break;
        default:
            return false;
    }

    HI_S32 ret = HI_MPI_RGN_Create(info.handle, &stRegionAttr);
    if (ret != HI_SUCCESS) {
        std::cerr << "HI_MPI_RGN_Create failed, ret=0x" << std::hex << ret << std::endl;
        return false;
    }
    return true;
}

bool OverlayRegionManager::attachRegionToChn(RegionInfo& info, const RegionDisplayAttr& attr) {
    RGN_CHN_ATTR_S stChnAttr;
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.bShow = HI_TRUE;

    switch (info.type) {
        case RegionType::OVERLAY:
            stChnAttr.enType = OVERLAY_RGN;
            stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = attr.x;
            stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = attr.y;
            stChnAttr.unChnAttr.stOverlayChn.u32Layer = attr.layer;
            stChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = attr.fgAlpha;
            stChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = attr.bgAlpha;
            break;
        case RegionType::OVERLAY_EX:
            stChnAttr.enType = OVERLAYEX_RGN;
            stChnAttr.unChnAttr.stOverlayExChn.stPoint.s32X = attr.x;
            stChnAttr.unChnAttr.stOverlayExChn.stPoint.s32Y = attr.y;
            stChnAttr.unChnAttr.stOverlayExChn.u32Layer = attr.layer;
            stChnAttr.unChnAttr.stOverlayExChn.u32FgAlpha = attr.fgAlpha;
            stChnAttr.unChnAttr.stOverlayExChn.u32BgAlpha = attr.bgAlpha;
            break;
        case RegionType::COVER:
            stChnAttr.enType = COVER_RGN;
            stChnAttr.unChnAttr.stCoverChn.enCoverType = AREA_RECT;
            stChnAttr.unChnAttr.stCoverChn.stRect.s32X = attr.x;
            stChnAttr.unChnAttr.stCoverChn.stRect.s32Y = attr.y;
            stChnAttr.unChnAttr.stCoverChn.stRect.u32Width = info.coverAttr.width;
            stChnAttr.unChnAttr.stCoverChn.stRect.u32Height = info.coverAttr.height;
            stChnAttr.unChnAttr.stCoverChn.u32Color = info.coverAttr.color;
            stChnAttr.unChnAttr.stCoverChn.enCoordinate = RGN_ABS_COOR;
            stChnAttr.unChnAttr.stCoverChn.u32Layer = attr.layer;
            break;
        case RegionType::COVER_EX:
            stChnAttr.enType = COVEREX_RGN;
            stChnAttr.unChnAttr.stCoverExChn.enCoverType = AREA_RECT;
            stChnAttr.unChnAttr.stCoverExChn.stRect.s32X = attr.x;
            stChnAttr.unChnAttr.stCoverExChn.stRect.s32Y = attr.y;
            stChnAttr.unChnAttr.stCoverExChn.stRect.u32Width = info.coverAttr.width;
            stChnAttr.unChnAttr.stCoverExChn.stRect.u32Height = info.coverAttr.height;
            stChnAttr.unChnAttr.stCoverExChn.u32Color = info.coverAttr.color;
            stChnAttr.unChnAttr.stCoverExChn.u32Layer = attr.layer;
            break;
        case RegionType::MOSAIC:
            stChnAttr.enType = MOSAIC_RGN;
            stChnAttr.unChnAttr.stMosaicChn.stRect.s32X = attr.x;
            stChnAttr.unChnAttr.stMosaicChn.stRect.s32Y = attr.y;
            stChnAttr.unChnAttr.stMosaicChn.stRect.u32Width = info.mosaicAttr.width;
            stChnAttr.unChnAttr.stMosaicChn.stRect.u32Height = info.mosaicAttr.height;
            stChnAttr.unChnAttr.stMosaicChn.enBlkSize = info.mosaicAttr.blkSize;
            stChnAttr.unChnAttr.stMosaicChn.u32Layer = attr.layer;
            break;
        default:
            return false;
    }

    HI_S32 ret = HI_MPI_RGN_AttachToChn(info.handle, &info.chn, &stChnAttr);
    if (ret != HI_SUCCESS) {
        std::cerr << "HI_MPI_RGN_AttachToChn failed, ret=0x" << std::hex << ret << std::endl;
        return false;
    }
    return true;
}

bool OverlayRegionManager::loadBmpToRegion(RGN_HANDLE handle, const RegionInfo& info, const std::string& filePath) {
    // 1. 获取BMP信息（宽、高、位深度）
    OSD_BITMAPFILEHEADER bmpFileHeader;
    OSD_BITMAPINFO bmpInfo;
    if (GetBmpInfo(filePath.c_str(), &bmpFileHeader, &bmpInfo) < 0) {
        std::cerr << "GetBmpInfo failed for " << filePath << std::endl;
        return false;
    }

    HI_U32 imgWidth = bmpInfo.bmiHeader.biWidth;
    HI_U32 imgHeight = std::abs(bmpInfo.bmiHeader.biHeight);
    HI_U16 bitCount = bmpInfo.bmiHeader.biBitCount;
    if (imgWidth != info.width || imgHeight != info.height) {
        std::cerr << "BMP size mismatch" << std::endl;
        return false;
    }
    if (bitCount != 24 && bitCount != 32) {
        std::cerr << "Only 24/32-bit BMP supported" << std::endl;
        return false;
    }

    // 2. 打开文件读取像素数据
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp) {
        std::cerr << "Cannot open BMP file" << std::endl;
        return false;
    }
    fseek(fp, bmpFileHeader.bfOffBits, SEEK_SET);

    HI_U32 rowSize = ((imgWidth * bitCount + 31) / 32) * 4;
    std::vector<HI_U8> bmpData(rowSize * imgHeight);
    if (fread(bmpData.data(), 1, bmpData.size(), fp) != bmpData.size()) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    // 3. 转换为目标像素格式 (ARGB1555 或 RGB565)
    HI_U32 bytesPerPixel = (info.pixelFormat == PIXEL_FORMAT_ARGB_8888) ? 4 : 2;
    std::vector<HI_U8> outData(info.width * info.height * bytesPerPixel);

    for (HI_U32 y = 0; y < imgHeight; ++y) {
        // BMP 存储为 bottom-up，通常需要翻转，这里按需选择
        HI_U32 srcRow = imgHeight - 1 - y; // 如果图像上下颠倒，改为 imgHeight - 1 - y
        const HI_U8* row = bmpData.data() + srcRow * rowSize;
        HI_U8* dst = outData.data() + y * info.width * bytesPerPixel;
        for (HI_U32 x = 0; x < imgWidth; ++x) {
            if (bitCount == 24) {
                HI_U32 b = row[x*3];
                HI_U32 g = row[x*3+1];
                HI_U32 r = row[x*3+2];
                if (info.pixelFormat == PIXEL_FORMAT_RGB_565) {
                    HI_U16 rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    *((HI_U16*)dst) = rgb565;
                } else if (info.pixelFormat == PIXEL_FORMAT_ARGB_1555) {
                    HI_U16 argb1555 = (1 << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                    *((HI_U16*)dst) = argb1555;
                }
                dst += 2;
            } else { // 32-bit
                HI_U32 b = row[x*4];
                HI_U32 g = row[x*4+1];
                HI_U32 r = row[x*4+2];
                HI_U32 a = row[x*4+3];
                if (info.pixelFormat == PIXEL_FORMAT_ARGB_8888) {
                    dst[x*4+0] = r;
                    dst[x*4+1] = g;
                    dst[x*4+2] = b;
                    dst[x*4+3] = a;
                } else if (info.pixelFormat == PIXEL_FORMAT_ARGB_1555) {
                    HI_U16 argb1555 = ((a > 128 ? 1 : 0) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                    *((HI_U16*)dst) = argb1555;
                    dst += 2;
                }
            }
        }
    }

    // 4. 设置位图
    BITMAP_S stBitmap;
    memset(&stBitmap, 0, sizeof(stBitmap));
    stBitmap.enPixelFormat = info.pixelFormat;
    stBitmap.u32Width = info.width;
    stBitmap.u32Height = info.height;
    stBitmap.pData = outData.data();

    HI_S32 ret = HI_MPI_RGN_SetBitMap(handle, &stBitmap);
    if (ret != HI_SUCCESS) {
        std::cerr << "HI_MPI_RGN_SetBitMap failed, ret=0x" << std::hex << ret << std::endl;
        return false;
    }
    return true;
}



// ==================== 公共创建接口 ====================
RegionId OverlayRegionManager::createOverlay(const MPP_CHN_S& chn,
                                             RegionType type,
                                             const std::string& bmpFilePath,
                                             PIXEL_FORMAT_E pixelFormat,
                                             const RegionDisplayAttr& attr,
                                             HI_U32 bgColor) {
    HI_U32 w, h;
    if (!getBmpSize(bmpFilePath, w, h)) {
        return 0;
    }
    return createOverlay(chn, type, w, h, bmpFilePath, pixelFormat, attr, bgColor);
}

RegionId OverlayRegionManager::createOverlay(const MPP_CHN_S& chn,
                                             RegionType type,
                                             HI_U32 width, HI_U32 height,
                                             const std::string& bmpFilePath,
                                             PIXEL_FORMAT_E pixelFormat,
                                             const RegionDisplayAttr& attr,
                                             HI_U32 bgColor) {
    if (type != RegionType::OVERLAY && type != RegionType::OVERLAY_EX) {
        return 0;
    }

    RegionId id = next_id_++;
    RGN_HANDLE handle = next_handle_++;

    RegionInfo info;
    info.handle = handle;
    info.chn = chn;
    info.type = type;
    info.width = width;
    info.height = height;
    info.pixelFormat = pixelFormat;

    if (!createRegionObject(info, bgColor)) {
        return 0;
    }
    if (!attachRegionToChn(info, attr)) {
        HI_MPI_RGN_Destroy(handle);
        return 0;
    }
    if (!loadBmpToRegion(handle, info, bmpFilePath)) {
        std::cerr << "Warning: loadBmpToRegion failed for " << bmpFilePath << std::endl;
    }

    info.attached = true;
    regions_[id] = info;
    chnToIds_[hashChn(chn)].push_back(id);
    return id;
}

RegionId OverlayRegionManager::createCover(const MPP_CHN_S& chn,
                                           RegionType type,
                                           const CoverAttr& coverAttr,
                                           const RegionDisplayAttr& attr) {
    if (type != RegionType::COVER && type != RegionType::COVER_EX) {
        return 0;
    }

    RegionId id = next_id_++;
    RGN_HANDLE handle = next_handle_++;

    RegionInfo info;
    info.handle = handle;
    info.chn = chn;
    info.type = type;
    info.coverAttr = coverAttr;

    if (!createRegionObject(info, 0)) {
        return 0;
    }
    if (!attachRegionToChn(info, attr)) {
        HI_MPI_RGN_Destroy(handle);
        return 0;
    }

    info.attached = true;
    regions_[id] = info;
    chnToIds_[hashChn(chn)].push_back(id);
    return id;
}

RegionId OverlayRegionManager::createMosaic(const MPP_CHN_S& chn,
                                            const MosaicAttr& mosaicAttr,
                                            const RegionDisplayAttr& attr) {
    RegionId id = next_id_++;
    RGN_HANDLE handle = next_handle_++;

    RegionInfo info;
    info.handle = handle;
    info.chn = chn;
    info.type = RegionType::MOSAIC;
    info.mosaicAttr = mosaicAttr;

    if (!createRegionObject(info, 0)) {
        return 0;
    }
    if (!attachRegionToChn(info, attr)) {
        HI_MPI_RGN_Destroy(handle);
        return 0;
    }

    info.attached = true;
    regions_[id] = info;
    chnToIds_[hashChn(chn)].push_back(id);
    return id;
}

// ==================== 公共操作 ====================
bool OverlayRegionManager::updateOverlayBitmap(RegionId id, const std::string& bmpFilePath) {
    auto it = regions_.find(id);
    if (it == regions_.end()) return false;
    const auto& info = it->second;
    if (info.type != RegionType::OVERLAY && info.type != RegionType::OVERLAY_EX) return false;

    HI_U32 w, h;
    if (!getBmpSize(bmpFilePath, w, h)) return false;
    if (w != info.width || h != info.height) {
        std::cerr << "Bitmap size mismatch" << std::endl;
        return false;
    }
    return loadBmpToRegion(info.handle, info, bmpFilePath);
}

bool OverlayRegionManager::setPosition(RegionId id, HI_S32 x, HI_S32 y) {
    auto it = regions_.find(id);
    if (it == regions_.end()) return false;
    auto& info = it->second;

    RGN_CHN_ATTR_S stChnAttr;
    HI_S32 ret = HI_MPI_RGN_GetDisplayAttr(info.handle, &info.chn, &stChnAttr);
    if (ret != HI_SUCCESS) return false;

    switch (info.type) {
        case RegionType::OVERLAY:
            stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = x;
            stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = y;
            break;
        case RegionType::OVERLAY_EX:
            stChnAttr.unChnAttr.stOverlayExChn.stPoint.s32X = x;
            stChnAttr.unChnAttr.stOverlayExChn.stPoint.s32Y = y;
            break;
        case RegionType::COVER:
            stChnAttr.unChnAttr.stCoverChn.stRect.s32X = x;
            stChnAttr.unChnAttr.stCoverChn.stRect.s32Y = y;
            break;
        case RegionType::COVER_EX:
            stChnAttr.unChnAttr.stCoverExChn.stRect.s32X = x;
            stChnAttr.unChnAttr.stCoverExChn.stRect.s32Y = y;
            break;
        case RegionType::MOSAIC:
            stChnAttr.unChnAttr.stMosaicChn.stRect.s32X = x;
            stChnAttr.unChnAttr.stMosaicChn.stRect.s32Y = y;
            break;
        default:
            return false;
    }
    ret = HI_MPI_RGN_SetDisplayAttr(info.handle, &info.chn, &stChnAttr);
    return (ret == HI_SUCCESS);
}

bool OverlayRegionManager::setAlpha(RegionId id, HI_U32 fgAlpha, HI_U32 bgAlpha) {
    auto it = regions_.find(id);
    if (it == regions_.end()) return false;
    auto& info = it->second;
    if (info.type != RegionType::OVERLAY && info.type != RegionType::OVERLAY_EX) return false;

    RGN_CHN_ATTR_S stChnAttr;
    HI_S32 ret = HI_MPI_RGN_GetDisplayAttr(info.handle, &info.chn, &stChnAttr);
    if (ret != HI_SUCCESS) return false;

    if (info.type == RegionType::OVERLAY) {
        stChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = fgAlpha;
        stChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = bgAlpha;
    } else {
        stChnAttr.unChnAttr.stOverlayExChn.u32FgAlpha = fgAlpha;
        stChnAttr.unChnAttr.stOverlayExChn.u32BgAlpha = bgAlpha;
    }
    ret = HI_MPI_RGN_SetDisplayAttr(info.handle, &info.chn, &stChnAttr);
    return (ret == HI_SUCCESS);
}

bool OverlayRegionManager::destroyRegion(RegionId id) {
    auto it = regions_.find(id);
    if (it == regions_.end()) return false;
    const auto& info = it->second;

    if (info.attached) {
        HI_MPI_RGN_DetachFromChn(info.handle, &info.chn);
    }
    HI_MPI_RGN_Destroy(info.handle);

    // 从通道索引中移除
    HI_U32 key = hashChn(info.chn);
    auto vecIt = chnToIds_.find(key);
    if (vecIt != chnToIds_.end()) {
        auto& vec = vecIt->second;
        vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
        if (vec.empty()) chnToIds_.erase(vecIt);
    }
    regions_.erase(it);
    return true;
}

void OverlayRegionManager::destroyRegionsOnChn(const MPP_CHN_S& chn) {
    HI_U32 key = hashChn(chn);
    auto it = chnToIds_.find(key);
    if (it == chnToIds_.end()) return;
    std::vector<RegionId> ids = it->second; // 拷贝
    for (RegionId id : ids) {
        destroyRegion(id);
    }
}

bool OverlayRegionManager::isValid(RegionId id) const {
    return regions_.find(id) != regions_.end();
}

} // namespace region
} // namespace hisi