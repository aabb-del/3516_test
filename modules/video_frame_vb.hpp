#pragma once

#include <stdexcept>
#include <functional>


// 添加OpenCV头文件支持
#ifndef OPENCV_CORE_HPP
#include <opencv2/opencv.hpp>
#endif

#include "mpp.hpp"


class VideoFrameVB {
public:
    // 构造函数申请VB块资源，支持带Cache的映射
    VideoFrameVB(HI_U32 u32Width = 1920, HI_U32 u32Height = 1080, bool useCache = true) 
        : m_u32Width(u32Width), m_u32Height(u32Height), m_useCache(useCache) {
        
        m_u32VBSize = u32Width * u32Height * 2;
        
        // 获取VB块
        m_vbHandle = HI_MPI_VB_GetBlock(VB_INVALID_POOLID, m_u32VBSize, HI_NULL);
        if (VB_INVALID_HANDLE == m_vbHandle) {
            SAMPLE_PRT("HI_MPI_VB_GetBlock failed!\n");
            throw std::runtime_error("Failed to get VB block");
        }

        // 获取物理地址
        m_u64PhyAddr = HI_MPI_VB_Handle2PhysAddr(m_vbHandle);
        if (0 == m_u64PhyAddr) {
            SAMPLE_PRT("HI_MPI_VB_Handle2PhysAddr failed!.\n");
            HI_MPI_VB_ReleaseBlock(m_vbHandle);
            throw std::runtime_error("Failed to get physical address");
        }

        SAMPLE_PRT("HI_MPI_SYS_Mmap u64PhyAddr %llx!.\n", m_u64PhyAddr);

        // 根据参数选择映射方式
        if (m_useCache) {
            // 带Cache的映射
            m_pu8VirAddr = (HI_U8*)HI_MPI_SYS_MmapCache(m_u64PhyAddr, m_u32VBSize);
        } else {
            // 不带Cache的映射
            m_pu8VirAddr = (HI_U8*)HI_MPI_SYS_Mmap(m_u64PhyAddr, m_u32VBSize);
        }
        
        if (HI_NULL == m_pu8VirAddr) {
            SAMPLE_PRT("HI_MPI_SYS_Mmap failed!.\n");
            HI_MPI_VB_ReleaseBlock(m_vbHandle);
            throw std::runtime_error("Failed to map memory");
        }

        // 初始化帧信息结构
        initFrameInfo();
    }

    // 析构函数自动释放资源
    ~VideoFrameVB() {
        if (m_pu8VirAddr != HI_NULL) {
            // 如果是带Cache的映射，可能需要刷新缓存
            if (m_useCache && m_dirty) {
                flushCache();
            }
            
            // 取消映射
            if (m_useCache) {
                HI_MPI_SYS_Munmap(m_pu8VirAddr, m_u32VBSize);
            } else {
                HI_MPI_SYS_Munmap(m_pu8VirAddr, m_u32VBSize);
            }
        }
        if (m_vbHandle != VB_INVALID_HANDLE) {
            HI_S32 s32Ret = HI_MPI_VB_ReleaseBlock(m_vbHandle);
            if(s32Ret != HI_SUCCESS)
            {
                printf("release vb block failed with 0x%0x!\n");
            }
        }

        printf("release vb block!\n");
        
    }

    // 禁止拷贝
    VideoFrameVB(const VideoFrameVB&) = delete;
    VideoFrameVB& operator=(const VideoFrameVB&) = delete;

    // 允许移动
    VideoFrameVB(VideoFrameVB&& other) noexcept 
        : m_vbHandle(other.m_vbHandle),
          m_u64PhyAddr(other.m_u64PhyAddr),
          m_pu8VirAddr(other.m_pu8VirAddr),
          m_u32Width(other.m_u32Width),
          m_u32Height(other.m_u32Height),
          m_u32VBSize(other.m_u32VBSize),
          m_useCache(other.m_useCache),
          m_dirty(other.m_dirty),
          m_frameInfo(other.m_frameInfo) {
        other.m_vbHandle = VB_INVALID_HANDLE;
        other.m_u64PhyAddr = 0;
        other.m_pu8VirAddr = HI_NULL;
    }

    VideoFrameVB& operator=(VideoFrameVB&& other) noexcept {
        if (this != &other) {
            // 释放当前资源
            this->~VideoFrameVB();
            
            // 转移资源
            m_vbHandle = other.m_vbHandle;
            m_u64PhyAddr = other.m_u64PhyAddr;
            m_pu8VirAddr = other.m_pu8VirAddr;
            m_u32Width = other.m_u32Width;
            m_u32Height = other.m_u32Height;
            m_u32VBSize = other.m_u32VBSize;
            m_useCache = other.m_useCache;
            m_dirty = other.m_dirty;
            m_frameInfo = other.m_frameInfo;
            
            // 置空原对象
            other.m_vbHandle = VB_INVALID_HANDLE;
            other.m_u64PhyAddr = 0;
            other.m_pu8VirAddr = HI_NULL;
        }
        return *this;
    }

    // 获取帧信息
    VIDEO_FRAME_INFO_S* getFrameInfo() {
        return &m_frameInfo;
    }

    const VIDEO_FRAME_INFO_S* getFrameInfo() const {
        return &m_frameInfo;
    }

    // 获取虚拟地址
    HI_U8* getVirAddr() const {
        return m_pu8VirAddr;
    }

    // 获取物理地址
    HI_U64 getPhyAddr() const {
        return m_u64PhyAddr;
    }

    // 获取宽度
    HI_U32 getWidth() const {
        return m_u32Width;
    }

    // 获取高度
    HI_U32 getHeight() const {
        return m_u32Height;
    }

    // 标记数据已修改（用于带Cache的映射）
    void markDirty() {
        m_dirty = true;
    }

    // 刷新缓存（确保数据同步到物理内存）
    void flushCache() {
        if (m_useCache && m_pu8VirAddr != HI_NULL) {
            HI_MPI_SYS_MflushCache(static_cast<HI_U32>(m_u64PhyAddr), m_pu8VirAddr, m_u32VBSize);
            m_dirty = false;
        }
    }

    // 执行操作并自动刷新缓存（RAII方式）
    template<typename Func>
    auto withCacheFlush(Func&& func) -> decltype(func()) {
        // 执行操作
        auto result = func();
        
        // 操作完成后刷新缓存
        if (m_useCache) {
            flushCache();
        }
        
        return result;
    }

    void loadFromMat(const cv::Mat& image, bool keepAspectRatio = true) {
        cv::Mat processedImage;
        
        // 检查图像尺寸是否匹配
        if (image.cols != static_cast<int>(m_u32Width) || 
            image.rows != static_cast<int>(m_u32Height)) {
            // 尺寸不匹配，进行缩放
            if (keepAspectRatio) {
                // 保持宽高比的缩放
                cv::Size targetSize(m_u32Width, m_u32Height);
                cv::Mat resized;
                double aspectRatio = static_cast<double>(image.cols) / image.rows;
                double targetAspectRatio = static_cast<double>(m_u32Width) / m_u32Height;
                
                if (aspectRatio > targetAspectRatio) {
                    // 宽度是限制因素
                    int newHeight = static_cast<int>(m_u32Width / aspectRatio);
                    cv::resize(image, resized, cv::Size(m_u32Width, newHeight));
                    
                    // 上下填充黑边
                    int padding = (m_u32Height - newHeight) / 2;
                    cv::copyMakeBorder(resized, processedImage, 
                                      padding, m_u32Height - newHeight - padding, 
                                      0, 0, cv::BORDER_CONSTANT, cv::Scalar(0));
                } else {
                    // 高度是限制因素
                    int newWidth = static_cast<int>(m_u32Height * aspectRatio);
                    cv::resize(image, resized, cv::Size(newWidth, m_u32Height));
                    
                    // 左右填充黑边
                    int padding = (m_u32Width - newWidth) / 2;
                    cv::copyMakeBorder(resized, processedImage, 
                                      0, 0, 
                                      padding, m_u32Width - newWidth - padding, 
                                      cv::BORDER_CONSTANT, cv::Scalar(0));
                }
            } else {
                // 不保持宽高比，直接拉伸
                cv::resize(image, processedImage, cv::Size(m_u32Width, m_u32Height));
            }
        } else {
            // 尺寸匹配，直接使用原图
            processedImage = image.clone();
        }
        
        // 根据通道数处理图像
        if (processedImage.channels() == 1) {
            // 灰度图像处理
            loadGrayImage(processedImage);
        } else if (processedImage.channels() == 3) {
            // 彩色图像处理
            loadColorImage(processedImage);
        } else {
            throw std::runtime_error("Unsupported number of channels");
        }

        // 标记数据已修改（用于带Cache的映射）
        if (m_useCache) {
            markDirty();
        }
    }
private:

    // 加载灰度图像
    void loadGrayImage(const cv::Mat& grayImage) {
        // 直接复制Y分量数据
        memcpy(m_pu8VirAddr, grayImage.data, m_u32Width * m_u32Height);
        
        // 设置UV分量（色度）为中性值（128）
        HI_U8* uvPlane = m_pu8VirAddr + m_u32Width * m_u32Height;
        memset(uvPlane, 128, m_u32Width * m_u32Height / 2);
    }

    // 加载彩色图像（BGR格式转换为YUV420SP）
    void loadColorImage(const cv::Mat& colorImage) {
        // 创建NV21格式的目标图像
        cv::Mat nv21Image;
        
        // 使用OpenCV将BGR转换为YUV，然后转换为NV21
        cv::cvtColor(colorImage, nv21Image, cv::COLOR_BGR2YUV_I420);
        
        // 将I420转换为NV21 (YUV420SP, VU交错)
        // I420布局: YYYYYYYY UU VV
        // NV21布局: YYYYYYYY VUVU
        
        // 复制Y平面
        memcpy(m_pu8VirAddr, nv21Image.data, m_u32Width * m_u32Height);
        
        // 处理UV平面 - 将I420的U和V平面交错为NV21的VU交错格式
        HI_U8* uvDst = m_pu8VirAddr + m_u32Width * m_u32Height;
        const HI_U8* uSrc = nv21Image.data + m_u32Width * m_u32Height;
        const HI_U8* vSrc = uSrc + (m_u32Width * m_u32Height) / 4;
        
        // 交错V和U分量 (NV21是VU交错，V在前)
        for (int i = 0; i < (m_u32Width * m_u32Height) / 4; i++) {
            uvDst[2*i] = vSrc[i];     // V分量
            uvDst[2*i+1] = uSrc[i];   // U分量
        }
    }

    void initFrameInfo() {
        m_frameInfo.enModId = HI_ID_USER;
        m_frameInfo.u32PoolId = HI_MPI_VB_Handle2PoolId(m_vbHandle);

        m_frameInfo.stVFrame.u32Width       = m_u32Width;
        m_frameInfo.stVFrame.u32Height      = m_u32Height;
        m_frameInfo.stVFrame.enField        = VIDEO_FIELD_FRAME;
        m_frameInfo.stVFrame.enPixelFormat  = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        m_frameInfo.stVFrame.enVideoFormat  = VIDEO_FORMAT_LINEAR;
        m_frameInfo.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
        m_frameInfo.stVFrame.enDynamicRange = DYNAMIC_RANGE_SDR8;
        m_frameInfo.stVFrame.enColorGamut   = COLOR_GAMUT_BT601;

        m_frameInfo.stVFrame.u32HeaderStride[0]  = m_u32Width;
        m_frameInfo.stVFrame.u32HeaderStride[1]  = m_u32Width;
        m_frameInfo.stVFrame.u32HeaderStride[2]  = m_u32Width;
        m_frameInfo.stVFrame.u64HeaderPhyAddr[0] = m_u64PhyAddr;
        m_frameInfo.stVFrame.u64HeaderPhyAddr[1] = m_frameInfo.stVFrame.u64HeaderPhyAddr[0] + m_u32Width * m_u32Height;
        m_frameInfo.stVFrame.u64HeaderPhyAddr[2] = m_frameInfo.stVFrame.u64HeaderPhyAddr[1];
        m_frameInfo.stVFrame.u64HeaderVirAddr[0] = (HI_U64)(HI_UL)m_pu8VirAddr;
        m_frameInfo.stVFrame.u64HeaderVirAddr[1] = m_frameInfo.stVFrame.u64HeaderVirAddr[0] + m_u32Width * m_u32Height;
        m_frameInfo.stVFrame.u64HeaderVirAddr[2] = m_frameInfo.stVFrame.u64HeaderVirAddr[1];

        m_frameInfo.stVFrame.u32Stride[0]  = m_u32Width;
        m_frameInfo.stVFrame.u32Stride[1]  = m_u32Width;
        m_frameInfo.stVFrame.u32Stride[2]  = m_u32Width;
        m_frameInfo.stVFrame.u64PhyAddr[0] = m_u64PhyAddr;
        m_frameInfo.stVFrame.u64PhyAddr[1] = m_frameInfo.stVFrame.u64PhyAddr[0] + m_u32Width * m_u32Height;
        m_frameInfo.stVFrame.u64PhyAddr[2] = m_frameInfo.stVFrame.u64PhyAddr[1];
        m_frameInfo.stVFrame.u64VirAddr[0] = (HI_U64)(HI_UL)m_pu8VirAddr;
        m_frameInfo.stVFrame.u64VirAddr[1] = m_frameInfo.stVFrame.u64VirAddr[0] + m_u32Width * m_u32Height;
        m_frameInfo.stVFrame.u64VirAddr[2] = m_frameInfo.stVFrame.u64VirAddr[1];
    }

    VB_BLK m_vbHandle = VB_INVALID_HANDLE;
    HI_U64 m_u64PhyAddr = 0;
    HI_U8* m_pu8VirAddr = HI_NULL;
    HI_U32 m_u32Width = 0;
    HI_U32 m_u32Height = 0;
    HI_U32 m_u32VBSize = 0;
    bool m_useCache = true;
    bool m_dirty = false; // 标记数据是否已修改（用于带Cache的映射）
    VIDEO_FRAME_INFO_S m_frameInfo = {};
};