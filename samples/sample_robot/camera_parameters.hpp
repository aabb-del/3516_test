// camera_parameters.hpp
#ifndef CAMERA_PARAMETERS_HPP
#define CAMERA_PARAMETERS_HPP

#include <opencv2/opencv.hpp>
#include <string>

class CameraParameters {
public:
    CameraParameters();
    ~CameraParameters();
    
    // 从文件加载参数 - 支持单个文件或两个独立文件
    bool loadFromFile(const std::string& filename); // 单个文件包含所有参数
    bool loadFromFiles(const std::string& intrinsics_file, 
                      const std::string& extrinsics_file); // 两个独立文件
    
    // 从矩阵加载参数
    bool loadFromMatrices(const cv::Mat& camera_matrix1, 
                         const cv::Mat& dist_coeffs1,
                         const cv::Mat& camera_matrix2,
                         const cv::Mat& dist_coeffs2,
                         const cv::Mat& R,
                         const cv::Mat& T);
    
    // 获取参数
    cv::Mat getCameraMatrix1() const;
    cv::Mat getDistCoeffs1() const;
    cv::Mat getCameraMatrix2() const;
    cv::Mat getDistCoeffs2() const;
    cv::Mat getRotation() const;
    cv::Mat getTranslation() const;

    
    // 保存参数 - 支持单个文件或两个独立文件
    bool saveToFile(const std::string& filename) const;
    bool saveToFiles(const std::string& intrinsics_file, 
                    const std::string& extrinsics_file) const;
    
    // 检查参数是否有效
    bool isValid() const;
    
    // 获取/设置图像尺寸（用于立体校正）
    cv::Size getImageSize() const;
    void setImageSize(const cv::Size& size);
    
private:
    cv::Mat camera_matrix1_;
    cv::Mat dist_coeffs1_;
    cv::Mat camera_matrix2_;
    cv::Mat dist_coeffs2_;
    cv::Mat R_;
    cv::Mat T_;
    cv::Size image_size_;
    bool is_valid_;
};

#endif // CAMERA_PARAMETERS_HPP