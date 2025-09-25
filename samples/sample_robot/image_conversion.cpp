// image_conversion.cpp
#include "image_conversion.h"
#include <iostream>

namespace ImageConversion {

// CPU实现
namespace CPU {

bool map_frame_memory(const VIDEO_FRAME_INFO_S& frame, void*& mapped_addr) {
    // 计算帧大小
    HI_U32 frame_size = 0;

    if(frame.stVFrame.enPixelFormat == PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
        frame_size = frame.stVFrame.u32Stride[0] * frame.stVFrame.u32Height * 3 / 2;
    } else {
        printf("Unsupported pixel format: %d\n", frame.stVFrame.enPixelFormat);
        return false;
    }

    mapped_addr = HI_MPI_SYS_MmapCache(
        reinterpret_cast<HI_U64>(frame.stVFrame.u64PhyAddr[0]),
        frame_size);
    
    if (mapped_addr == NULL) {
        printf("Map frame memory failed!\n");
        return false;
    }
    
    return true;
}

void unmap_frame_memory(const VIDEO_FRAME_INFO_S& frame, void* mapped_addr) {
    if (mapped_addr) {
        // 计算帧大小
        HI_U32 frame_size = 0;

        if(frame.stVFrame.enPixelFormat == PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
            frame_size = frame.stVFrame.u32Stride[0] * frame.stVFrame.u32Height * 3 / 2;
        }
        
        HI_MPI_SYS_Munmap(mapped_addr, frame_size);
    }
}

bool yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img) {
    // 获取图像信息
    int width = yuv_frame.stVFrame.u32Width;
    int height = yuv_frame.stVFrame.u32Height;
    
    // 映射内存
    void* mapped_addr = nullptr;
    if (!map_frame_memory(yuv_frame, mapped_addr)) {
        return false;
    }
    
    try {
        // 根据像素格式处理
        if (yuv_frame.stVFrame.enPixelFormat == PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
            // NV12格式 - 直接提取Y分量
            cv::Mat y_plane(height, width, CV_8UC1, mapped_addr);
            gray_img = y_plane.clone(); // 使用clone确保数据独立
        } else {
            printf("Unsupported pixel format: %d\n", yuv_frame.stVFrame.enPixelFormat);
            unmap_frame_memory(yuv_frame, mapped_addr);
            return false;
        }
        
        // 取消映射
        unmap_frame_memory(yuv_frame, mapped_addr);
        return true;
    } catch (const cv::Exception& e) {
        printf("YUV to gray conversion failed: %s\n", e.what());
        unmap_frame_memory(yuv_frame, mapped_addr);
        return false;
    }
}

bool yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img) {
    // 获取图像信息
    int width = yuv_frame.stVFrame.u32Width;
    int height = yuv_frame.stVFrame.u32Height;
    
    // 映射内存
    void* mapped_addr = nullptr;
    if (!map_frame_memory(yuv_frame, mapped_addr)) {
        return false;
    }
    
    try {
        // 根据像素格式处理
        if (yuv_frame.stVFrame.enPixelFormat == PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
            // NV12格式
            cv::Mat yuv_img(height * 3 / 2, width, CV_8UC1, mapped_addr);
            cv::cvtColor(yuv_img, bgr_img, cv::COLOR_YUV2BGR_NV21);
        } else {
            printf("Unsupported pixel format: %d\n", yuv_frame.stVFrame.enPixelFormat);
            unmap_frame_memory(yuv_frame, mapped_addr);
            return false;
        }
        
        // 取消映射
        unmap_frame_memory(yuv_frame, mapped_addr);
        return true;
    } catch (const cv::Exception& e) {
        printf("YUV to BGR conversion failed: %s\n", e.what());
        unmap_frame_memory(yuv_frame, mapped_addr);
        return false;
    }
}

bool save_vi_frame_as_jpg(const VIDEO_FRAME_INFO_S& yuv_frame, const std::string& filename) {
    cv::Mat bgr_img;
    if (!yuv_to_bgr(yuv_frame, bgr_img)) {
        return false;
    }
    
    // 确保目录存在
    size_t pos = filename.find_last_of("/");
    if (pos != std::string::npos) {
        std::string dir = filename.substr(0, pos);
        system(("mkdir -p " + dir).c_str());
    }
    
    // 保存图像
    return cv::imwrite(filename, bgr_img);
}

} // namespace CPU


// 硬件加速实现（预留）
namespace Hardware {
// 未来可以在这里添加硬件加速的实现
// 例如使用海思的IVE或VPSS硬件加速单元
} // namespace Hardware

// 通用接口实现
bool yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img, bool use_hardware) {
    if (use_hardware) {
        // 未来可以调用硬件加速版本
        // return Hardware::yuv_to_gray(yuv_frame, gray_img);
        printf("Hardware acceleration not implemented yet, using CPU fallback\n");
    }
    
    // 默认使用CPU实现
    return CPU::yuv_to_gray(yuv_frame, gray_img);
}

bool yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img, bool use_hardware) {
    if (use_hardware) {
        // 未来可以调用硬件加速版本
        // return Hardware::yuv_to_bgr(yuv_frame, bgr_img);
        printf("Hardware acceleration not implemented yet, using CPU fallback\n");
    }
    
    // 默认使用CPU实现
    return CPU::yuv_to_bgr(yuv_frame, bgr_img);
}

} // namespace ImageConversion