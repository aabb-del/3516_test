// hi_stereo_match.cpp
#include "hi_stereo_match.hpp"
#include <iostream>
#include <cstring>

#include "mpi_sys.h"

#define ALIGN16(x) (((x) + 15) & ~15)

HiStereoMatcher::HiStereoMatcher(const Parameters& params, const CameraParameters& cam_params) 
    : params_(params), camera_params_(cam_params), isInitialized_(false),
      leftGray_(nullptr), rightGray_(nullptr), rectifiedLeft_(nullptr), 
      rectifiedRight_(nullptr), tempBuffer1_(nullptr), tempBuffer2_(nullptr) {
    
    isInitialized_ = camera_params_.isValid();
    if (!isInitialized_) {
        printf("Warning: Camera parameters are not valid.\n");
    }
}

HiStereoMatcher::~HiStereoMatcher() {
    releaseIveResources();
}

bool HiStereoMatcher::initIveResources(int width, int height) {
    if (!params_.useIve) return false;

    releaseIveResources();

    // 创建灰度图像缓冲区
    leftGray_ = new IVE_IMAGE_S();
    rightGray_ = new IVE_IMAGE_S();
    rectifiedLeft_ = new IVE_IMAGE_S();
    rectifiedRight_ = new IVE_IMAGE_S();
    tempBuffer1_ = new IVE_IMAGE_S();
    tempBuffer2_ = new IVE_IMAGE_S();

    // 初始化图像结构
    memset(leftGray_, 0, sizeof(IVE_IMAGE_S));
    memset(rightGray_, 0, sizeof(IVE_IMAGE_S));
    memset(rectifiedLeft_, 0, sizeof(IVE_IMAGE_S));
    memset(rectifiedRight_, 0, sizeof(IVE_IMAGE_S));
    memset(tempBuffer1_, 0, sizeof(IVE_IMAGE_S));
    memset(tempBuffer2_, 0, sizeof(IVE_IMAGE_S));

    leftGray_->enType = IVE_IMAGE_TYPE_U8C1;
    leftGray_->u32Width = width;
    leftGray_->u32Height = height;
    leftGray_->au32Stride[0] = ALIGN16(width);

    rightGray_->enType = IVE_IMAGE_TYPE_U8C1;
    rightGray_->u32Width = width;
    rightGray_->u32Height = height;
    rightGray_->au32Stride[0] = ALIGN16(width);

    // 分配内存
    HI_MPI_SYS_MmzAlloc(&leftGray_->au64PhyAddr[0], (HI_VOID**)&leftGray_->au64VirAddr[0], "IVE_Buffer", HI_NULL, leftGray_->au32Stride[0] * height);
    HI_MPI_SYS_MmzAlloc(&rightGray_->au64PhyAddr[0], (HI_VOID**)&rightGray_->au64VirAddr[0],    "IVE_Buffer" , HI_NULL,rightGray_->au32Stride[0] * height);

    return true;
}

void HiStereoMatcher::releaseIveResources() {
    auto freeImage = [](IVE_IMAGE_S* img) {
        if (img && img->au64VirAddr[0]) {
            HI_MPI_SYS_MmzFree(img->au64PhyAddr[0], (HI_VOID*)img->au64VirAddr[0]);
            delete img;
        }
    };

    freeImage(leftGray_);
    freeImage(rightGray_);
    freeImage(rectifiedLeft_);
    freeImage(rectifiedRight_);
    freeImage(tempBuffer1_);
    freeImage(tempBuffer2_);

    leftGray_ = rightGray_ = rectifiedLeft_ = rectifiedRight_ = tempBuffer1_ = tempBuffer2_ = nullptr;
}

bool HiStereoMatcher::yuvToGray(IVE_IMAGE_S* yuvImage, IVE_IMAGE_S* grayImage) {
    if (!params_.useIve) return false;

    IVE_CSC_CTRL_S ctrl;
    memset(&ctrl, 0, sizeof(IVE_CSC_CTRL_S));
    ctrl.enMode = IVE_CSC_MODE_PIC_BT709_YUV2RGB;
    // ctrl.enOutFmt = IVE_CSC_OUT_FMT_ARGB8888;

    IVE_DST_IMAGE_S dstRgb;
    memset(&dstRgb, 0, sizeof(IVE_DST_IMAGE_S));
    // dstRgb.enType = IVE_IMAGE_TYPE_U8C4_PACKAGE;
    dstRgb.u32Width = grayImage->u32Width;
    dstRgb.u32Height = grayImage->u32Height;
    dstRgb.au32Stride[0] = ALIGN16(grayImage->u32Width * 4);

    HI_MPI_SYS_MmzAlloc(&dstRgb.au64PhyAddr[0], (HI_VOID**)&dstRgb.au64VirAddr[0], "IVE_RGB", HI_NULL, dstRgb.au32Stride[0] * grayImage->u32Height);

    HI_S32 ret = HI_MPI_IVE_CSC(&iveHandle_, yuvImage, &dstRgb, &ctrl, HI_TRUE);
    if (ret != HI_SUCCESS) {
        HI_MPI_SYS_MmzFree(dstRgb.au64PhyAddr[0], (HI_VOID*)dstRgb.au64VirAddr[0]);
        return false;
    }

    // 从RGB提取Y分量（灰度）
    IVE_FILTER_CTRL_S filterCtrl;
    memset(&filterCtrl, 0, sizeof(IVE_FILTER_CTRL_S));
    filterCtrl.u8Norm = 7;
    // filterCtrl.au8Coef[0] = 54;  // R分量系数
    // filterCtrl.au8Coef[1] = 183; // G分量系数  
    // filterCtrl.au8Coef[2] = 19;  // B分量系数

    ret = HI_MPI_IVE_Filter(&iveHandle_, &dstRgb, grayImage, &filterCtrl, HI_TRUE);
    
    HI_MPI_SYS_MmzFree(dstRgb.au64PhyAddr[0], (HI_VOID*)dstRgb.au64VirAddr[0]);
    return ret == HI_SUCCESS;
}

// NEON加速的Sobel算子
void HiStereoMatcher::neonSobel3x3(const uint8_t* src, int16_t* dst, int width, int height, int stride) {
    if (!params_.useNeon) return;

    for (int y = 1; y < height - 1; y++) {
        const uint8_t* srcRow0 = src + (y - 1) * stride;
        const uint8_t* srcRow1 = src + y * stride;
        const uint8_t* srcRow2 = src + (y + 1) * stride;
        int16_t* dstRow = dst + y * stride;

        int x = 1;
        for (; x <= width - 8; x += 8) {
            // uint8x8_t top = vld1_u8(srcRow0 + x - 1);
            // uint8x8_t mid = vld1_u8(srcRow1 + x - 1);
            // uint8x8_t bot = vld1_u8(srcRow2 + x - 1);

            // // Sobel X: [-1,0,1; -2,0,2; -1,0,1]
            // int16x8_t gx = vsubq_s16(
            //     vaddq_s16(vaddq_s16(
            //         vshlq_n_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(top))), 1),
            //         vshlq_n_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(bot))), 1)
            //     ), vreinterpretq_s16_u16(vmovl_u8(mid))),
            //     vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(top)))
            // );

            // // Sobel Y: [-1,-2,-1; 0,0,0; 1,2,1]
            // int16x8_t gy = vsubq_s16(
            //     vaddq_s16(vaddq_s16(
            //         vshlq_n_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(top))), 1),
            //         vshlq_n_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(bot))), 1)
            //     ), vreinterpretq_s16_u16(vmovl_u8(mid))),
            //     vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(bot)))
            // );

            // int16x8_t magnitude = vaddq_s16(vabsq_s16(gx), vabsq_s16(gy));
            // vst1q_s16(dstRow + x, magnitude);
        }

        // 处理剩余像素
        for (; x < width - 1; x++) {
            int16_t gx = -srcRow0[x-1] + srcRow0[x+1] - 2*srcRow1[x-1] + 2*srcRow1[x+1] - srcRow2[x-1] + srcRow2[x+1];
            int16_t gy = -srcRow0[x-1] - 2*srcRow0[x] - srcRow0[x+1] + srcRow2[x-1] + 2*srcRow2[x] + srcRow2[x+1];
            dstRow[x] = abs(gx) + abs(gy);
        }
    }
}

// NEON加速的Census变换
void HiStereoMatcher::neonComputeCensus(const uint8_t* left, const uint8_t* right, 
                                      uint32_t* censusLeft, uint32_t* censusRight, 
                                      int width, int height) {
    const int censusWidth = 9;
    const int censusHeight = 7;
    const int hw = censusWidth / 2;
    const int hh = censusHeight / 2;

    for (int y = hh; y < height - hh; y++) {
        for (int x = hw; x < width - hw; x += 4) {
            uint8x16_t centerLeft = vld1q_u8(left + y * width + x);
            uint8x16_t centerRight = vld1q_u8(right + y * width + x);

            uint32x4_t censusL = vdupq_n_u32(0);
            uint32x4_t censusR = vdupq_n_u32(0);
            uint32_t bit = 0;

            for (int dy = -hh; dy <= hh; dy++) {
                for (int dx = -hw; dx <= hw; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    uint8x16_t neighborLeft = vld1q_u8(left + (y + dy) * width + x + dx);
                    uint8x16_t neighborRight = vld1q_u8(right + (y + dy) * width + x + dx);
                    
                    uint8x16_t cmpLeft = vcgtq_u8(neighborLeft, centerLeft);
                    uint8x16_t cmpRight = vcgtq_u8(neighborRight, centerRight);
                    
                    uint32x4_t bitMask = vdupq_n_u32(1 << bit);
                    censusL = vorrq_u32(censusL, vandq_u32(vreinterpretq_u32_u8(cmpLeft), bitMask));
                    censusR = vorrq_u32(censusR, vandq_u32(vreinterpretq_u32_u8(cmpRight), bitMask));
                    bit++;
                }
            }
            vst1q_u32(censusLeft + y * width + x, censusL);
            vst1q_u32(censusRight + y * width + x, censusR);
        }
    }
}

bool HiStereoMatcher::computeDisparityBM_NEON(const uint8_t* left, const uint8_t* right, 
                                            int16_t* disparity, int width, int height) {
    if (!params_.useNeon) return false;

    const int maxDisp = params_.maxDisparity;
    const int blockSize = params_.blockSize;
    const int hw = blockSize / 2;

    // 为每行分配临时内存
    int16_t* sadRow = new int16_t[width * maxDisp];
    
    for (int y = hw; y < height - hw; y++) {
        // 预计算SAD
        for (int x = hw; x < width - hw; x++) {
            for (int d = 0; d < maxDisp; d++) {
                if (x - d < hw) {
                    sadRow[y * width * maxDisp + x * maxDisp + d] = SHRT_MAX;
                    continue;
                }

                int sad = 0;
                // 使用NEON加速SAD计算
                for (int dy = -hw; dy <= hw; dy++) {
                    const uint8_t* leftPtr = left + (y + dy) * width + x;
                    const uint8_t* rightPtr = right + (y + dy) * width + x - d;
                    
                    int dx = 0;
                    uint8x16_t sadAccum = vdupq_n_u8(0);
                    
                    for (; dx <= blockSize - 16; dx += 16) {
                        uint8x16_t leftPixels = vld1q_u8(leftPtr + dx);
                        uint8x16_t rightPixels = vld1q_u8(rightPtr + dx);
                        uint8x16_t diff = vabdq_u8(leftPixels, rightPixels);
                        sadAccum = vaddq_u8(sadAccum, diff);
                    }
                    
                    // 水平求和
                    // uint16_t rowSAD = vaddvq_u8(sadAccum);
                    // for (; dx < blockSize; dx++) {
                    //     rowSAD += abs(leftPtr[dx] - rightPtr[dx]);
                    // }
                    // sad += rowSAD;
                }
                sadRow[y * width * maxDisp + x * maxDisp + d] = sad;
            }
        }

        // 寻找最佳视差
        for (int x = hw; x < width - hw; x++) {
            int16_t bestDisparity = 0;
            int16_t bestSad = SHRT_MAX;
            
            for (int d = 0; d < maxDisp; d++) {
                if (sadRow[y * width * maxDisp + x * maxDisp + d] < bestSad) {
                    bestSad = sadRow[y * width * maxDisp + x * maxDisp + d];
                    bestDisparity = d;
                }
            }
            disparity[y * width + x] = bestDisparity;
        }
    }

    delete[] sadRow;
    return true;
}

bool HiStereoMatcher::computeDisparity(const cv::Mat& left, const cv::Mat& right, cv::Mat& disparity) {
    if (left.size() != right.size() || left.type() != CV_8UC1) {
        return false;
    }

    cv::Mat leftGray = left;
    cv::Mat rightGray = right;
    
    // 缩放处理
    if (params_.scale != 1.0f) {
        cv::resize(leftGray, leftGray, cv::Size(), params_.scale, params_.scale);
        cv::resize(rightGray, rightGray, cv::Size(), params_.scale, params_.scale);
    }

    int width = leftGray.cols;
    int height = leftGray.rows;
    
    disparity.create(height, width, CV_16S);
    
    if (params_.useNeon) {
        return computeDisparityBM_NEON(leftGray.data, rightGray.data, 
                                     (int16_t*)disparity.data, width, height);
    } else {
        // 回退到OpenCV实现
        StereoMatcher::Parameters cvParams;
        // cvParams.algorithm = params_.algorithm;
        // cvParams.maxDisparity = params_.maxDisparity;
        // cvParams.blockSize = params_.blockSize;
        // cvParams.scale = params_.scale;
        
        StereoMatcher matcher(cvParams, camera_params_);
        return matcher.computeDisparity(left, right, disparity);
    }
}

bool HiStereoMatcher::computeDisparityYUV(IVE_IMAGE_S* leftYuv, IVE_IMAGE_S* rightYuv, IVE_IMAGE_S* disparity) {
    if (!leftYuv || !rightYuv || !disparity) return false;

    int width = leftYuv->u32Width;
    int height = leftYuv->u32Height;

    // 初始化IVE资源
    if (!initIveResources(width, height)) {
        return false;
    }

    // YUV转灰度
    if (!yuvToGray(leftYuv, leftGray_) || !yuvToGray(rightYuv, rightGray_)) {
        return false;
    }

    // 立体匹配
    cv::Mat leftMat(height, width, CV_8UC1, leftGray_->au64VirAddr[0]);
    cv::Mat rightMat(height, width, CV_8UC1, rightGray_->au64VirAddr[0]);
    cv::Mat dispMat;
    
    bool success = computeDisparity(leftMat, rightMat, dispMat);
    
    if (success) {
        // 将结果拷贝回IVE图像
        memcpy((void*)disparity->au64VirAddr[0], dispMat.data, width * height * sizeof(int16_t));
    }
    
    return success;
}

// 其他接口实现保持不变（复用原有代码）
bool HiStereoMatcher::computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud) {
    // 复用原有实现
    StereoMatcher::Parameters cvParams;
    // cvParams.algorithm = params_.algorithm;
    StereoMatcher matcher(cvParams, camera_params_);
    return matcher.computePointCloud(disparity, pointCloud);
}

bool HiStereoMatcher::computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud, const cv::Mat& Q) {
    StereoMatcher::Parameters cvParams;
    // cvParams.algorithm = params_.algorithm;
    StereoMatcher matcher(cvParams, camera_params_);
    return matcher.computePointCloud(disparity, pointCloud, Q);
}

bool HiStereoMatcher::computePointCloudYUV(IVE_IMAGE_S* disparity, cv::Mat& pointCloud) {
    if (!disparity) return false;
    
    cv::Mat dispMat(disparity->u32Height, disparity->u32Width, CV_16S, disparity->au64VirAddr[0]);
    return computePointCloud(dispMat, pointCloud);
}

bool HiStereoMatcher::savePointCloud(const std::string& filename, const cv::Mat& pointCloud) {
    StereoMatcher::Parameters cvParams;
    // cvParams.algorithm = params_.algorithm;
    StereoMatcher matcher(cvParams, camera_params_);
    return matcher.savePointCloud(filename, pointCloud);
}

bool HiStereoMatcher::toDisplay(cv::Mat& disparity_origion, cv::Mat& disparity_8u) {
    StereoMatcher::Parameters cvParams;
    // cvParams.algorithm = params_.algorithm;
    StereoMatcher matcher(cvParams, camera_params_);
    return matcher.toDisplay(disparity_origion, disparity_8u);
}

CameraParameters HiStereoMatcher::getCameraParameters() const {
    return camera_params_;
}

cv::Mat HiStereoMatcher::getQMatrix() const {
    return last_Q_;
}

// 复用原有的校正映射函数
void HiStereoMatcher::getRectificationMaps(cv::Mat& left_map1, cv::Mat& left_map2,
                                          cv::Mat& right_map1, cv::Mat& right_map2,
                                          cv::Size& image_size, RectificationParams& rect_params) {
    // 实现与原有StereoMatcher相同
}

CameraParameters HiStereoMatcher::adjustCameraParametersForScale(float scale) const {
    // 实现与原有StereoMatcher相同
    CameraParameters adjusted = camera_params_;
    return adjusted;
}