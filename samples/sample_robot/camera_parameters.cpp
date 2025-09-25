// camera_parameters.cpp
#include "camera_parameters.hpp"
#include <iostream>

#include <unistd.h>

CameraParameters::CameraParameters() : is_valid_(false), image_size_(0, 0) {
}

CameraParameters::~CameraParameters() {
}


cv::Mat CameraParameters::getCameraMatrix1() const {
    return camera_matrix1_;
}

cv::Mat CameraParameters::getDistCoeffs1() const {
    return dist_coeffs1_;
}

cv::Mat CameraParameters::getCameraMatrix2() const {
    return camera_matrix2_;
}

cv::Mat CameraParameters::getDistCoeffs2() const {
    return dist_coeffs2_;
}

cv::Mat CameraParameters::getRotation() const {
    return R_;
}

cv::Mat CameraParameters::getTranslation() const {
    return T_;
}

// 暂时忽略
// cv::Mat CameraParameters::getQMatrix() const {
//     return Q_;
// }

cv::Size CameraParameters::getImageSize() const {
    return image_size_;
}

void CameraParameters::setImageSize(const cv::Size& size) {
    image_size_ = size;
}


bool CameraParameters::isValid() const {
    return is_valid_;
}


// 修改 loadFromMatrices 方法（移除 Q）
bool CameraParameters::loadFromMatrices(const cv::Mat& camera_matrix1, 
                                      const cv::Mat& dist_coeffs1,
                                      const cv::Mat& camera_matrix2,
                                      const cv::Mat& dist_coeffs2,
                                      const cv::Mat& R,
                                      const cv::Mat& T) {
    camera_matrix1_ = camera_matrix1.clone();
    dist_coeffs1_ = dist_coeffs1.clone();
    camera_matrix2_ = camera_matrix2.clone();
    dist_coeffs2_ = dist_coeffs2.clone();
    R_ = R.clone();
    T_ = T.clone();
    
    is_valid_ = !camera_matrix1_.empty() && !camera_matrix2_.empty() && 
                !R_.empty() && !T_.empty();
    
    return is_valid_;
}


bool CameraParameters::loadFromFile(const std::string& filename) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "Failed to open camera parameters file: " << filename << std::endl;
        return false;
    }
    
    fs["M1"] >> camera_matrix1_;
    fs["D1"] >> dist_coeffs1_;
    fs["M2"] >> camera_matrix2_;
    fs["D2"] >> dist_coeffs2_;
    fs["R"] >> R_;
    fs["T"] >> T_;
    
    // 尝试读取图像尺寸
    cv::FileNode size_node = fs["image_size"];
    if (!size_node.empty()) {
        int width, height;
        size_node["width"] >> width;
        size_node["height"] >> height;
        image_size_ = cv::Size(width, height);
    }
    
    fs.release();
    
    // 验证参数（移除 Q 的检查）
    is_valid_ = !camera_matrix1_.empty() && !camera_matrix2_.empty();
    
    if (!is_valid_) {
        std::cerr << "Loaded camera parameters are incomplete or invalid" << std::endl;
    }
    
    return is_valid_;
}


bool CameraParameters::loadFromFiles(const std::string& intrinsics_file, 
                                   const std::string& extrinsics_file) {
    // ... 文件存在性检查保持不变 ...
    
    // 加载内参文件
    cv::FileStorage fs_intrinsic(intrinsics_file, cv::FileStorage::READ);
    if (!fs_intrinsic.isOpened()) {
        std::cerr << "Failed to open intrinsics file: " << intrinsics_file << std::endl;
        return false;
    }

    // 检查必需的参数（移除 Q）
    if (fs_intrinsic["M1"].empty() || fs_intrinsic["D1"].empty() || 
        fs_intrinsic["M2"].empty() || fs_intrinsic["D2"].empty()) {
        std::cerr << "Intrinsics file is missing required parameters: " << intrinsics_file << std::endl;
        fs_intrinsic.release();
        return false;
    }
    
    fs_intrinsic["M1"] >> camera_matrix1_;
    fs_intrinsic["D1"] >> dist_coeffs1_;
    fs_intrinsic["M2"] >> camera_matrix2_;
    fs_intrinsic["D2"] >> dist_coeffs2_;
    
    // 尝试读取图像尺寸
    cv::FileNode size_node = fs_intrinsic["image_size"];
    if (!size_node.empty()) {
        int width, height;
        size_node["width"] >> width;
        size_node["height"] >> height;
        image_size_ = cv::Size(width, height);
    }
    
    fs_intrinsic.release();
    
    // 加载外参文件（移除 Q）
    cv::FileStorage fs_extrinsic(extrinsics_file, cv::FileStorage::READ);
    if (!fs_extrinsic.isOpened()) {
        std::cerr << "Failed to open extrinsics file: " << extrinsics_file << std::endl;
        return false;
    }
    
    fs_extrinsic["R"] >> R_;
    fs_extrinsic["T"] >> T_;
    
    fs_extrinsic.release();
    
    // 验证参数（移除 Q 的检查）
    is_valid_ = !camera_matrix1_.empty() && !camera_matrix2_.empty() && 
                !R_.empty() && !T_.empty();
    
    if (!is_valid_) {
        std::cerr << "Loaded camera parameters are incomplete or invalid" << std::endl;
    }
    
    return is_valid_;
}


bool CameraParameters::saveToFile(const std::string& filename) const {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    if (!fs.isOpened()) {
        return false;
    }
    
    fs << "M1" << camera_matrix1_;
    fs << "D1" << dist_coeffs1_;
    fs << "M2" << camera_matrix2_;
    fs << "D2" << dist_coeffs2_;
    fs << "R" << R_;
    fs << "T" << T_;
    
    // 保存图像尺寸
    fs << "image_size" << "{";
    fs << "width" << image_size_.width;
    fs << "height" << image_size_.height;
    fs << "}";
    
    fs.release();
    return true;
}


bool CameraParameters::saveToFiles(const std::string& intrinsics_file, 
                                 const std::string& extrinsics_file) const {
    // 保存内参文件
    cv::FileStorage fs_intrinsic(intrinsics_file, cv::FileStorage::WRITE);
    if (!fs_intrinsic.isOpened()) {
        return false;
    }
    
    fs_intrinsic << "M1" << camera_matrix1_;
    fs_intrinsic << "D1" << dist_coeffs1_;
    fs_intrinsic << "M2" << camera_matrix2_;
    fs_intrinsic << "D2" << dist_coeffs2_;
    
    // 保存图像尺寸到内参文件
    fs_intrinsic << "image_size" << "{";
    fs_intrinsic << "width" << image_size_.width;
    fs_intrinsic << "height" << image_size_.height;
    fs_intrinsic << "}";
    
    fs_intrinsic.release();
    
    // 保存外参文件（移除 Q）
    cv::FileStorage fs_extrinsic(extrinsics_file, cv::FileStorage::WRITE);
    if (!fs_extrinsic.isOpened()) {
        return false;
    }
    
    fs_extrinsic << "R" << R_;
    fs_extrinsic << "T" << T_;
    
    fs_extrinsic.release();
    
    return true;
}