// stereo_match.hpp
#ifndef STEREO_MATCH_HPP
#define STEREO_MATCH_HPP

#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/core/utility.hpp"
#include "camera_parameters.hpp"
#include <string>

class StereoMatcher {
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
        int maxDisparity = 0;  // 0 means auto-calculate
        int blockSize = 0;     // 0 means use default
        float scale = 1.0f;
    };

    // 添加用于存储校正参数的结构体
    struct RectificationParams {
        cv::Mat R1, R2, P1, P2;
        cv::Rect roi1, roi2;
        cv::Mat Q;
    };

    StereoMatcher(const Parameters& params, const CameraParameters& cam_params);
    ~StereoMatcher();

    bool computeDisparity(const cv::Mat& left, const cv::Mat& right, cv::Mat& disparity);

    // 添加 Q 参数
    bool computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud, const cv::Mat& Q);
    // 在内部使用最近计算的 Q
    bool computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud);

    bool savePointCloud(const std::string& filename, const cv::Mat& pointCloud);
    bool toDisplay(cv::Mat& disparity_origion, cv::Mat& disparity_8u);

    CameraParameters getCameraParameters() const;

    // 添加获取 Q 矩阵的方法
    cv::Mat getQMatrix() const;

private:
    Parameters params_;
    CameraParameters camera_params_;
    cv::Ptr<cv::StereoBM> bm_;
    cv::Ptr<cv::StereoSGBM> sgbm_;
    // 存储最近计算的 Q 矩阵
    cv::Mat last_Q_;
    bool isInitialized_;

    void getRectificationMaps(cv::Mat& left_map1, cv::Mat& left_map2,
                             cv::Mat& right_map1, cv::Mat& right_map2,
                             cv::Size& image_size,
                             RectificationParams& rect_params);
    
    // 添加辅助方法用于调整相机参数
    CameraParameters adjustCameraParametersForScale(float scale) const;
};
#endif // STEREO_MATCH_HPP