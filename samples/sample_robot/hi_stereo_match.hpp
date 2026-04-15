// hi_stereo_match.hpp
#ifndef HI_STEREO_MATCH_HPP
#define HI_STEREO_MATCH_HPP

#include "stereo_match.hpp"
#include "camera_parameters.hpp"
#include "hi_comm_ive.h"
#include "hi_comm_video.h"
#include "mpi_ive.h"
#include "hi_math.h"
#include <arm_neon.h>

class HiStereoMatcher {
public:
    enum Algorithm {
        STEREO_BM = 0,
        STEREO_SGBM = 1,
        STEREO_HH = 2,
        STEREO_HH4 = 5,
        STEREO_3WAY = 4
    };

    struct Parameters {
        Algorithm algorithm = STEREO_SGBM;
        int maxDisparity = 64;
        int blockSize = 9;
        float scale = 1.0f;
        bool useIve = true;
        bool useNeon = true;
    };

    struct RectificationParams {
        cv::Mat R1, R2, P1, P2;
        cv::Rect roi1, roi2;
        cv::Mat Q;
    };

    HiStereoMatcher(const Parameters& params, const CameraParameters& cam_params);
    ~HiStereoMatcher();

    // OpenCV Mat接口
    bool computeDisparity(const cv::Mat& left, const cv::Mat& right, cv::Mat& disparity);
    bool computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud);
    bool computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud, const cv::Mat& Q);
    bool savePointCloud(const std::string& filename, const cv::Mat& pointCloud);
    bool toDisplay(cv::Mat& disparity_origion, cv::Mat& disparity_8u);

    // 海思YUV接口
    bool computeDisparityYUV(IVE_IMAGE_S* leftYuv, IVE_IMAGE_S* rightYuv, IVE_IMAGE_S* disparity);
    bool computePointCloudYUV(IVE_IMAGE_S* disparity, cv::Mat& pointCloud);
    
    CameraParameters getCameraParameters() const;
    cv::Mat getQMatrix() const;

private:
    Parameters params_;
    CameraParameters camera_params_;
    cv::Mat last_Q_;
    bool isInitialized_;

    // IVE相关资源
    IVE_HANDLE iveHandle_;
    IVE_IMAGE_S* leftGray_;
    IVE_IMAGE_S* rightGray_;
    IVE_IMAGE_S* rectifiedLeft_;
    IVE_IMAGE_S* rectifiedRight_;
    IVE_IMAGE_S* tempBuffer1_;
    IVE_IMAGE_S* tempBuffer2_;

    // 初始化IVE资源
    bool initIveResources(int width, int height);
    void releaseIveResources();
    
    // IVE处理函数
    bool yuvToGray(IVE_IMAGE_S* yuvImage, IVE_IMAGE_S* grayImage);
    bool remapImage(IVE_IMAGE_S* src, IVE_IMAGE_S* dst, const cv::Mat& mapx, const cv::Mat& mapy);
    
    // NEON加速函数
    void neonSobel3x3(const uint8_t* src, int16_t* dst, int width, int height, int stride);
    void neonBoxFilter5x5(const uint8_t* src, uint8_t* dst, int width, int height, int stride);
    void neonComputeCensus(const uint8_t* left, const uint8_t* right, uint32_t* censusLeft, 
                          uint32_t* censusRight, int width, int height);
    
    // 立体匹配核心函数
    bool computeDisparityBM_NEON(const uint8_t* left, const uint8_t* right, 
                                int16_t* disparity, int width, int height);
    bool computeDisparitySGBM_NEON(const uint8_t* left, const uint8_t* right,
                                  int16_t* disparity, int width, int height);

    void getRectificationMaps(cv::Mat& left_map1, cv::Mat& left_map2,
                             cv::Mat& right_map1, cv::Mat& right_map2,
                             cv::Size& image_size, RectificationParams& rect_params);
    
    CameraParameters adjustCameraParametersForScale(float scale) const;
};

#endif // HI_STEREO_MATCH_HPP