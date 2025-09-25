// stereo_match.cpp
#include "stereo_match.hpp"
#include <stdio.h>

StereoMatcher::StereoMatcher(const Parameters& params, const CameraParameters& cam_params) 
    : params_(params), camera_params_(cam_params), isInitialized_(false) {
    
    bm_ = cv::StereoBM::create(16, 9);
    sgbm_ = cv::StereoSGBM::create(0, 16, 3);
    
    isInitialized_ = camera_params_.isValid();
    if (!isInitialized_) {
        printf("Warning: Camera parameters are not valid. Stereo matching may not work correctly.\n");
        isInitialized_ = true;
    }

    // 初始化 last_Q_ 为空矩阵
    last_Q_ = cv::Mat();
}

StereoMatcher::~StereoMatcher() {
    // Cleanup if needed
}



CameraParameters StereoMatcher::adjustCameraParametersForScale(float scale) const {
    if (scale == 1.0f || !camera_params_.isValid()) {
        return camera_params_;
    }
    
    CameraParameters adjusted_params;
    
    // 获取原始参数
    cv::Mat M1 = camera_params_.getCameraMatrix1();
    cv::Mat D1 = camera_params_.getDistCoeffs1();
    cv::Mat M2 = camera_params_.getCameraMatrix2();
    cv::Mat D2 = camera_params_.getDistCoeffs2();
    cv::Mat R = camera_params_.getRotation();
    cv::Mat T = camera_params_.getTranslation();
    
    
    // 调整内参矩阵 - 与OpenCV示例一致
    cv::Mat M1_adjusted = M1 * scale;
    cv::Mat M2_adjusted = M2 * scale;

    // 调整图像尺寸
    cv::Size original_size = camera_params_.getImageSize();
    cv::Size adjusted_size(
        cvRound(original_size.width * scale),
        cvRound(original_size.height * scale)
    );

    
    adjusted_params.loadFromMatrices(
        M1_adjusted, D1, 
        M2_adjusted, D2, 
        R, T
    );
    adjusted_params.setImageSize(adjusted_size);
    
    return adjusted_params;
}

void StereoMatcher::getRectificationMaps(cv::Mat& left_map1, cv::Mat& left_map2,
                                        cv::Mat& right_map1, cv::Mat& right_map2,
                                        cv::Size& image_size,
                                        RectificationParams& rect_params) {
    if (!camera_params_.isValid()) {
        return;
    }
    
    // 根据缩放因子调整相机参数
    CameraParameters adjusted_cam_params = adjustCameraParametersForScale(params_.scale);
    
    cv::Mat M1 = adjusted_cam_params.getCameraMatrix1();
    cv::Mat D1 = adjusted_cam_params.getDistCoeffs1();
    cv::Mat M2 = adjusted_cam_params.getCameraMatrix2();
    cv::Mat D2 = adjusted_cam_params.getDistCoeffs2();
    cv::Mat R = adjusted_cam_params.getRotation();
    cv::Mat T = adjusted_cam_params.getTranslation();
    
    // 如果图像尺寸未设置，使用默认值
    if (image_size.width == 0 || image_size.height == 0) {
        image_size = adjusted_cam_params.getImageSize();
        if (image_size.width == 0 || image_size.height == 0) {
            image_size = cv::Size(640, 480);
        }
    }
    
    // 计算立体校正参数
    cv::stereoRectify(M1, D1, M2, D2, image_size, R, T, 
                     rect_params.R1, rect_params.R2, rect_params.P1, rect_params.P2, 
                     rect_params.Q, cv::CALIB_ZERO_DISPARITY, -1, image_size, 
                     &rect_params.roi1, &rect_params.roi2);

    // 保存最近计算的 Q 矩阵
    last_Q_ = rect_params.Q.clone();
    
    // 计算校正映射
    cv::initUndistortRectifyMap(M1, D1, rect_params.R1, rect_params.P1, 
                               image_size, CV_16SC2, left_map1, left_map2);
    cv::initUndistortRectifyMap(M2, D2, rect_params.R2, rect_params.P2, 
                               image_size, CV_16SC2, right_map1, right_map2);
}


bool StereoMatcher::computeDisparity(const cv::Mat& left, const cv::Mat& right, cv::Mat& disparity) {
    if (!isInitialized_) {
        printf("StereoMatcher not initialized properly\n");
        return false;
    }

    cv::Mat img1 = left.clone();
    cv::Mat img2 = right.clone();
    cv::Size original_size = img1.size();

    // 应用比例缩放（如果需要）
    if (params_.scale != 1.0f) {
        int method = params_.scale < 1 ? cv::INTER_AREA : cv::INTER_CUBIC;
        cv::resize(img1, img1, cv::Size(), params_.scale, params_.scale, method);
        cv::resize(img2, img2, cv::Size(), params_.scale, params_.scale, method);
    }

    cv::Size img_size = img1.size();
    RectificationParams rect_params;

    // 如果有有效的相机参数，校正图像
    if (camera_params_.isValid()) {
        cv::Mat left_map1, left_map2, right_map1, right_map2;
        
        // 获取校正映射和校正参数
        getRectificationMaps(left_map1, left_map2, right_map1, right_map2, img_size, rect_params);
        
        cv::Mat img1r, img2r;
        cv::remap(img1, img1r, left_map1, left_map2, cv::INTER_LINEAR);
        cv::remap(img2, img2r, right_map1, right_map2, cv::INTER_LINEAR);

        img1 = img1r;
        img2 = img2r;
    }

    // 自动计算视差范围（如果需要）
    int numberOfDisparities = params_.maxDisparity;
    if (numberOfDisparities <= 0) {
        numberOfDisparities = ((img1.cols / 8) + 15) & -16;
    }

    // 根据算法类型初始化参数
    if (params_.algorithm == STEREO_BM) {
        int SADWindowSize = params_.blockSize > 0 ? params_.blockSize : 9;

        bm_->setPreFilterCap(31);
        bm_->setBlockSize(SADWindowSize);
        bm_->setMinDisparity(0);
        bm_->setNumDisparities(numberOfDisparities);
        bm_->setTextureThreshold(10);
        bm_->setUniquenessRatio(15);
        bm_->setSpeckleWindowSize(100);
        bm_->setSpeckleRange(32);
        bm_->setDisp12MaxDiff(1);
        
        // 如果有有效的相机参数，设置ROI
        if (camera_params_.isValid()) {
            bm_->setROI1(rect_params.roi1);
            bm_->setROI2(rect_params.roi2);
        }
    } else {
        int sgbmWinSize = params_.blockSize > 0 ? params_.blockSize : 3;
        
        sgbm_->setPreFilterCap(63);
        sgbm_->setBlockSize(sgbmWinSize);
        sgbm_->setP1(8 * sgbmWinSize * sgbmWinSize);
        sgbm_->setP2(32 * sgbmWinSize * sgbmWinSize);
        sgbm_->setMinDisparity(0);
        sgbm_->setNumDisparities(numberOfDisparities);
        sgbm_->setUniquenessRatio(10);
        sgbm_->setSpeckleWindowSize(100);
        sgbm_->setSpeckleRange(32);
        sgbm_->setDisp12MaxDiff(1);
        
        switch (params_.algorithm) {
            case STEREO_HH:
                sgbm_->setMode(cv::StereoSGBM::MODE_HH);
                break;
            case STEREO_HH4:
                sgbm_->setMode(cv::StereoSGBM::MODE_HH4);
                break;
            case STEREO_3WAY:
                sgbm_->setMode(cv::StereoSGBM::MODE_SGBM_3WAY);
                break;
            default:
                sgbm_->setMode(cv::StereoSGBM::MODE_SGBM);
                break;
        }
    }

    // 计算视差
    if (params_.algorithm == STEREO_BM) {
        bm_->compute(img1, img2, disparity);
    } else {
        sgbm_->compute(img1, img2, disparity);
    }

    return true;
}

bool StereoMatcher::toDisplay(cv::Mat& disparity_origion, cv::Mat& disparity_8u) {
    // 转换为8位用于显示
    if (params_.algorithm != STEREO_HH) {
        disparity_origion.convertTo(disparity_8u, CV_8U, 255 / (params_.maxDisparity * 16.0));
    } else {
        disparity_origion.convertTo(disparity_8u, CV_8U);
    }

    return true;
}

bool StereoMatcher::computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud) {
    if (!camera_params_.isValid()) {
        printf("Camera parameters are not available for point cloud computation\n");
        return false;
    }

    // Q矩阵是在stereoRectify函数里面计算出来的，如果缩放的话我们直接使用就行了
    if (last_Q_.empty()) {
        printf("Reprojection matrix Q is not available\n");
        return false;
    }

    float disparity_multiplier = 1.0f;
    if (disparity.type() == CV_16S)
        disparity_multiplier = 16.0f;


    cv::Mat floatDisp;
    disparity.convertTo(floatDisp, CV_32F, 1.0 / disparity_multiplier);
    cv::reprojectImageTo3D(floatDisp, pointCloud, last_Q_, true);

    return true;
}

bool StereoMatcher::computePointCloud(const cv::Mat& disparity, cv::Mat& pointCloud, const cv::Mat& Q) {
    if (Q.empty()) {
        printf("Reprojection matrix Q is not available\n");
        return false;
    }

    float disparity_multiplier = 1.0f;
    if (disparity.type() == CV_16S)
        disparity_multiplier = 16.0f;

    cv::Mat floatDisp;
    disparity.convertTo(floatDisp, CV_32F, 1.0 / disparity_multiplier);
    cv::reprojectImageTo3D(floatDisp, pointCloud, Q, true);

    return true;
}


bool StereoMatcher::savePointCloud(const std::string& filename, const cv::Mat& pointCloud) {
    const double max_z = 1.0e4;
    FILE* fp = fopen(filename.c_str(), "wt");
    if (!fp) {
        printf("Failed to open file for writing: %s\n", filename.c_str());
        return false;
    }

    for (int y = 0; y < pointCloud.rows; y++) {
        for (int x = 0; x < pointCloud.cols; x++) {
            cv::Vec3f point = pointCloud.at<cv::Vec3f>(y, x);
            if (fabs(point[2] - max_z) < FLT_EPSILON || fabs(point[2]) > max_z) continue;
            fprintf(fp, "%f %f %f\n", point[0], point[1], point[2]);
        }
    }
    
    fclose(fp);
    return true;
}

CameraParameters StereoMatcher::getCameraParameters() const {
    return camera_params_;
}