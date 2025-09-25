// image_conversion.h
#ifndef IMAGE_CONVERSION_H
#define IMAGE_CONVERSION_H

#include <hi_common.h>
#include <hi_comm_video.h>
#include <mpi_sys.h>
#include <opencv2/opencv.hpp>
#include <string>

namespace ImageConversion {

// CPU实现命名空间
namespace CPU {
    /**
     * @brief 映射海思帧内存到用户空间
     * @param frame 海思帧
     * @param mapped_addr 映射后的地址
     * @return 是否成功映射
     */
    bool map_frame_memory(const VIDEO_FRAME_INFO_S& frame, void*& mapped_addr);
    
    /**
     * @brief 取消映射海思帧内存
     * @param frame 海思帧
     * @param mapped_addr 映射的地址
     */
    void unmap_frame_memory(const VIDEO_FRAME_INFO_S& frame, void* mapped_addr);
    
    /**
     * @brief YUV转灰度图（CPU实现）
     * @param yuv_frame YUV图像帧
     * @param gray_img 输出灰度图像
     * @return 是否成功转换
     */
    bool yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img);
    
    /**
     * @brief YUV转BGR图像（CPU实现）
     * @param yuv_frame YUV图像帧
     * @param bgr_img 输出BGR图像
     * @return 是否成功转换
     */
    bool yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img);
    
    /**
     * @brief 保存VI图像为JPG文件（CPU实现）
     * @param yuv_frame YUV图像帧
     * @param filename 保存的文件名
     * @return 是否成功保存
     */
    bool save_vi_frame_as_jpg(const VIDEO_FRAME_INFO_S& yuv_frame, const std::string& filename);
}

// 硬件加速实现命名空间（预留）
namespace Hardware {
    // 预留硬件加速版本的函数声明
    // bool yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img);
    // bool yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img);
    // bool save_vi_frame_as_jpg(const VIDEO_FRAME_INFO_S& yuv_frame, const std::string& filename);
}

// 通用接口（可根据需要选择实现）
/**
 * @brief YUV转灰度图（自动选择实现）
 * @param yuv_frame YUV图像帧
 * @param gray_img 输出灰度图像
 * @param use_hardware 是否使用硬件加速
 * @return 是否成功转换
 */
bool yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img, bool use_hardware = false);

/**
 * @brief YUV转BGR图像（自动选择实现）
 * @param yuv_frame YUV图像帧
 * @param bgr_img 输出BGR图像
 * @param use_hardware 是否使用硬件加速
 * @return 是否成功转换
 */
bool yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img, bool use_hardware = false);

} // namespace ImageConversion

#endif // IMAGE_CONVERSION_H