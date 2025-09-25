#include "stereo_calibration.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <hi_comm_venc.h>
#include "mpi_sys.h"
#include "hi_common.h"
#include "mpi_vi.h"
#include "mpi_isp.h"

#include "sample_comm.h"
#include "expand.h"





// 构造函数
StereoCalibration::StereoCalibration(int left_pipe, int left_chn, 
                                   int right_pipe, int right_chn,
                                   int board_width, int board_height, 
                                   float square_size)
    : left_pipe_(left_pipe), left_chn_(left_chn),
      right_pipe_(right_pipe), right_chn_(right_chn),
      board_width_(board_width), board_height_(board_height),
      square_size_(square_size), calibrated_(false) ,
      should_exit_(false),
      exit_callback_(nullptr) {
}

// 析构函数
StereoCalibration::~StereoCalibration() {
}

// 从VI通道获取图像
bool StereoCalibration::get_frame_from_vi(int pipe, int chn, std::unique_ptr<VIFrameRAII>& frame_raii) {
    frame_raii = std::make_unique<VIFrameRAII>(pipe, chn);
    return frame_raii->acquire();
}



// 映射海思帧内存到用户空间
bool StereoCalibration::map_frame_memory(const VIDEO_FRAME_INFO_S& frame, void*& mapped_addr) {
    // 计算帧大小
    HI_U32 frame_size = 0;

    if(frame.stVFrame.enPixelFormat == PIXEL_FORMAT_YVU_SEMIPLANAR_420)
    {
        frame_size = frame.stVFrame.u32Stride[0] * frame.stVFrame.u32Height * 3 / 2;
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

// 取消映射海思帧内存
void StereoCalibration::unmap_frame_memory(const VIDEO_FRAME_INFO_S& frame, void* mapped_addr) {
    if (mapped_addr) {
        // 计算帧大小
        HI_U32 frame_size = 0;

        if(frame.stVFrame.enPixelFormat == PIXEL_FORMAT_YVU_SEMIPLANAR_420)
        {
            frame_size = frame.stVFrame.u32Stride[0] * frame.stVFrame.u32Height * 3 / 2;
        }
        
        HI_MPI_SYS_Munmap(mapped_addr, frame_size);
    }
}

// YUV转BGR
bool StereoCalibration::yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img) {
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
            cv::cvtColor(yuv_img, bgr_img, cv::COLOR_YUV2BGR_NV12);
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

// YUV转灰度图（只提取Y分量）
bool StereoCalibration::yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img) {
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
            // 创建一个新的Mat对象，复制Y分量数据
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

// 检测棋盘格角点（使用灰度图）
bool StereoCalibration::find_chessboard_corners(const cv::Mat& img, std::vector<cv::Point2f>& corners) {
    cv::Size board_size(board_width_, board_height_);
    
    // 检查图像是否有效
    if (img.empty()) {
        printf("Error: Empty image passed to find_chessboard_corners\n");
        return false;
    }
    
    // 确保图像是灰度图
    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else if (img.channels() == 1) {
        gray = img;
    } else {
        printf("Error: Unsupported number of channels: %d\n", img.channels());
        return false;
    }
    
    // 检查图像尺寸是否合理
    if (gray.cols < board_width_ || gray.rows < board_height_) {
        printf("Error: Image size (%dx%d) is too small for board size (%dx%d)\n", 
               gray.cols, gray.rows, board_width_, board_height_);
        return false;
    }
    
    printf("Finding chessboard corners in %dx%d image...\n", gray.cols, gray.rows);
    
    // 保存调试图像
    static int debug_count = 0;
    char debug_filename[100];
    snprintf(debug_filename, sizeof(debug_filename), "debug_gray_%03d.jpg", debug_count++);
    cv::imwrite(debug_filename, gray);
    printf("Saved debug image: %s\n", debug_filename);
    

    // 降低图像分辨率（例如缩小到一半）
    cv::Mat small_gray;
    double scale = 0.5; // 缩放因子
    cv::resize(gray, small_gray, cv::Size(), scale, scale, cv::INTER_AREA);
    
    std::vector<cv::Point2f> small_corners;
    
    // 首先尝试快速检查
    bool found = cv::findChessboardCorners(small_gray, board_size, small_corners, 
                                        cv::CALIB_CB_FAST_CHECK);

    if (!found) {
        // 快速检查没找到，再尝试更详细的方法
        found = cv::findChessboardCorners(small_gray, board_size, small_corners, 
                                        cv::CALIB_CB_ADAPTIVE_THRESH | 
                                        cv::CALIB_CB_NORMALIZE_IMAGE);
    }
    

    if (found) {
        printf("Found %lu corners on reduced image, refining...\n", small_corners.size());
        
        // 将角点坐标映射回原始图像尺寸
        corners.clear();
        for (const auto& corner : small_corners) {
            corners.push_back(corner * (1.0 / scale));
        }
        
        // 在原始图像上精化角点
        cv::cornerSubPix(gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
                        cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 0.01));
        
        printf("Corners refined successfully\n");

    } else {
            printf("Chessboard corners not found in reduced image, ignore...\n");
            // printf("Chessboard corners not found in reduced image, trying full resolution...\n");
            // // 如果在缩小图像中没找到，尝试原图
            // found = cv::findChessboardCorners(gray, board_size, corners, 
            //                                 cv::CALIB_CB_ADAPTIVE_THRESH | 
            //                                 cv::CALIB_CB_NORMALIZE_IMAGE);
            
            // if (found && !detection_cancelled) {
            //     printf("Found %lu corners, refining...\n", corners.size());
            //     cv::cornerSubPix(gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
            //                     cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 0.01));
            // }
    }


    if(!found){
        printf("Chessboard corners not found\n");
    }

    return found;
}



// 异步角点检测函数
bool StereoCalibration::find_chessboard_corners_async(const cv::Mat& img, std::vector<cv::Point2f>& corners) {
    cv::Size board_size(board_width_, board_height_);
    
    // 检查图像是否有效
    if (img.empty()) {
        printf("Error: Empty image passed to find_chessboard_corners\n");
        return false;
    }
    
    // 确保图像是灰度图
    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else if (img.channels() == 1) {
        gray = img;
    } else {
        printf("Error: Unsupported number of channels: %d\n", img.channels());
        return false;
    }


    // 异步执行角点检测
    auto detection_task = [&gray, board_size, &corners, this]() -> bool {
        
        // 降低图像分辨率（例如缩小到一半）
        cv::Mat small_gray;
        double scale = 0.5; // 缩放因子
        cv::resize(gray, small_gray, cv::Size(), scale, scale, cv::INTER_AREA);
        
        std::vector<cv::Point2f> small_corners;
        
        // 首先尝试快速检查
        bool found = cv::findChessboardCorners(small_gray, board_size, small_corners, 
                                            cv::CALIB_CB_FAST_CHECK);

        if (!found) {
            // 快速检查没找到，再尝试更详细的方法
            found = cv::findChessboardCorners(small_gray, board_size, small_corners, 
                                            cv::CALIB_CB_ADAPTIVE_THRESH | 
                                            cv::CALIB_CB_NORMALIZE_IMAGE);
        }
        

        if (found && !detection_cancelled) {
            printf("Found %lu corners on reduced image, refining...\n", small_corners.size());
            
            // 将角点坐标映射回原始图像尺寸
            corners.clear();
            for (const auto& corner : small_corners) {
                corners.push_back(corner * (1.0 / scale));
            }
            
            // 在原始图像上精化角点
            cv::cornerSubPix(gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
                            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 0.01));
            
            printf("Corners refined successfully\n");

        } else {
            printf("Chessboard corners not found in reduced image, ignore...\n");
            // printf("Chessboard corners not found in reduced image, trying full resolution...\n");
            // // 如果在缩小图像中没找到，尝试原图
            // found = cv::findChessboardCorners(gray, board_size, corners, 
            //                                 cv::CALIB_CB_ADAPTIVE_THRESH | 
            //                                 cv::CALIB_CB_NORMALIZE_IMAGE);
            
            // if (found && !detection_cancelled) {
            //     printf("Found %lu corners, refining...\n", corners.size());
            //     cv::cornerSubPix(gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
            //                     cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 10, 0.01));
            // }
        }
        
        return found && !detection_cancelled;
    };
    
    // 启动异步任务
    auto future = std::async(std::launch::async, detection_task);
    
    // 等待结果，但定期检查退出条件
    while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        if (should_exit_) {
            detection_cancelled = true;
            return false;
        }
    }
    
    return future.get();
}



// 采集标定图像
int StereoCalibration::capture_calibration_images(int num_frames, const std::string& save_path, bool use_y_channel_only) {
    int captured = 0;
    left_image_points_.clear();
    right_image_points_.clear();
    object_points_.clear();
    
    // 准备世界坐标系中的角点坐标
    std::vector<cv::Point3f> obj;
    for (int i = 0; i < board_height_; i++) {
        for (int j = 0; j < board_width_; j++) {
            obj.push_back(cv::Point3f(j * square_size_, i * square_size_, 0));
        }
    }
    
    printf("Starting to capture calibration images. Need %d pairs.\n", num_frames);
    printf("Press 's' to save a pair, 'q' to quit.\n");
    printf("Using %s for corner detection.\n", use_y_channel_only ? "Y channel only" : "BGR image");
    
    // 创建保存目录
    if (!save_path.empty()) {
        system(("mkdir -p " + save_path).c_str());
    }
    

    
    while (captured < num_frames && (!exit_callback_ || !exit_callback_())) {
        printf("Try get images...\n");
        
        // 使用RAII包装获取帧
        std::unique_ptr<VIFrameRAII> left_frame, right_frame;
        bool got_left = get_frame_from_vi(left_pipe_, left_chn_, left_frame);
        bool got_right = get_frame_from_vi(right_pipe_, right_chn_, right_frame);
        
        if (!got_left || !got_right) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        printf("Try send images to vo...\n");
   
        HI_MPI_VO_SendFrame(0, 0, left_frame->get(), -1);
        HI_MPI_VO_SendFrame(0, 1, right_frame->get(), -1);
        
        printf("Try convert images...\n");
        // 转换为BGR或灰度图
        cv::Mat left_img, right_img;
        bool conversion_success = false;
        
        if (use_y_channel_only) {
            // 只使用Y分量作为灰度图
            conversion_success = yuv_to_gray(*(left_frame->get()), left_img) && 
                                 yuv_to_gray(*(right_frame->get()), right_img);
        } else {
            // 使用完整的BGR图像
            conversion_success = yuv_to_bgr(*(left_frame->get()), left_img) && 
                                 yuv_to_bgr(*(right_frame->get()), right_img);
        }
        
        if (!conversion_success) {
            printf("Image conversion failed\n");
            continue;
        }
        
        // 记录图像尺寸
        if (image_size_.width == 0) {
            image_size_ = left_img.size();
            printf("Image size: %dx%d\n", image_size_.width, image_size_.height);
        }
        
        printf("Try 检测角点...\n");
        // 检测角点
        std::vector<cv::Point2f> left_corners, right_corners;
        bool left_found = false, right_found = false;
        
        try {
            left_found = find_chessboard_corners_async(left_img, left_corners);
            right_found = find_chessboard_corners_async(right_img, right_corners);
        } catch (const std::exception& e) {
            printf("Exception during corner detection: %s\n", e.what());
            continue;
        }
        
        printf("角点检测结果: left=%d, right=%d\n", left_found, right_found);
        
        // 保存调试图像
        if (!save_path.empty()) {
            cv::Mat left_debug, right_debug;
            if (use_y_channel_only) {
                // 如果是灰度图，转换为彩色显示
                cv::cvtColor(left_img, left_debug, cv::COLOR_GRAY2BGR);
                cv::cvtColor(right_img, right_debug, cv::COLOR_GRAY2BGR);
            } else {
                left_debug = left_img.clone();
                right_debug = right_img.clone();
            }
            
            if (left_found) {
                cv::drawChessboardCorners(left_debug, cv::Size(board_width_, board_height_), 
                                        left_corners, left_found);
            }
            
            if (right_found) {
                cv::drawChessboardCorners(right_debug, cv::Size(board_width_, board_height_), 
                                        right_corners, right_found);
            }
            
            char debug_filename[256];
            snprintf(debug_filename, sizeof(debug_filename), "%s/debug_left_%03d.jpg", save_path.c_str(), captured);
            cv::imwrite(debug_filename, left_debug);
            
            snprintf(debug_filename, sizeof(debug_filename), "%s/debug_right_%03d.jpg", save_path.c_str(), captured);
            cv::imwrite(debug_filename, right_debug);
            
            printf("Debug images saved to %s\n", save_path.c_str());
        }
        
        // 等待用户输入
        printf("等待按键 (s:保存, q:退出)\n");
        char key = getchar();
        // int key = 's';

        if (key == 's' || key == 'S') {
            if (left_found && right_found) {
                left_image_points_.push_back(left_corners);
                right_image_points_.push_back(right_corners);
                object_points_.push_back(obj);
                captured++;
                
                printf("Captured pair %d/%d\n", captured, num_frames);
                
                // 保存图像（保存为彩色图像）
                if (!save_path.empty()) {
                    cv::Mat left_bgr, right_bgr;
                    if (use_y_channel_only) {
                        // 如果是灰度图，转换为彩色保存
                        cv::cvtColor(left_img, left_bgr, cv::COLOR_GRAY2BGR);
                        cv::cvtColor(right_img, right_bgr, cv::COLOR_GRAY2BGR);
                    } else {
                        left_bgr = left_img;
                        right_bgr = right_img;
                    }
                    
                    char filename[256];
                    snprintf(filename, sizeof(filename), "%s/left_%02d.jpg", save_path.c_str(), captured);
                    cv::imwrite(filename, left_bgr);
                    
                    snprintf(filename, sizeof(filename), "%s/right_%02d.jpg", save_path.c_str(), captured);
                    cv::imwrite(filename, right_bgr);
                    
                    printf("Calibration images saved to %s\n", save_path.c_str());
                }
            } else {
                printf("Chessboard not found in one or both images!\n");
            }
        } else if (key == 'q' || key == 'Q') {
            printf("Capture interrupted by user.\n");
            break;
        }
        
        // 不需要手动释放帧，RAII对象会在离开作用域时自动释放
    }
    
    printf("Capture completed. Total pairs: %d\n", captured);
    return captured;
}

// 执行双目标定
bool StereoCalibration::perform_calibration(const std::string& save_path) {
    if (object_points_.size() < 5) {
        printf("Not enough calibration data. Need at least 5 pairs.\n");
        return false;
    }
    
    printf("Starting stereo calibration with %lu image pairs...\n", object_points_.size());
    
    // 执行双目标定
    double rms = cv::stereoCalibrate(object_points_, left_image_points_, right_image_points_,
                                   camera_matrix1_, dist_coeffs1_,
                                   camera_matrix2_, dist_coeffs2_,
                                   image_size_, R_, T_, E_, F_,
                                   cv::CALIB_FIX_ASPECT_RATIO +
                                   cv::CALIB_ZERO_TANGENT_DIST +
                                   cv::CALIB_SAME_FOCAL_LENGTH +
                                   cv::CALIB_RATIONAL_MODEL +
                                   cv::CALIB_FIX_K3 + cv::CALIB_FIX_K4 + cv::CALIB_FIX_K5,
                                   cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-6));
    
    printf("Stereo calibration done. RMS error = %f\n", rms);
    
    // 检查标定质量
    double avg_err = 0;
    int total_points = 0;
    std::vector<cv::Point2f> left_proj, right_proj;
    
    for (size_t i = 0; i < object_points_.size(); i++) {
        // 投影左图像点
        cv::projectPoints(object_points_[i], cv::Mat::eye(3, 3, CV_64F), cv::Mat::zeros(3, 1, CV_64F),
                         camera_matrix1_, dist_coeffs1_, left_proj);
        
        // 投影右图像点
        cv::projectPoints(object_points_[i], R_, T_, camera_matrix2_, dist_coeffs2_, right_proj);
        
        // 计算左图像误差
        double err = cv::norm(cv::Mat(left_image_points_[i]), cv::Mat(left_proj), cv::NORM_L2);
        avg_err += err * err;
        total_points += object_points_[i].size();
        
        // 计算右图像误差
        err = cv::norm(cv::Mat(right_image_points_[i]), cv::Mat(right_proj), cv::NORM_L2);
        avg_err += err * err;
    }
    
    avg_err = std::sqrt(avg_err / (2 * total_points));
    printf("Average reprojection error = %f\n", avg_err);
    
    calibrated_ = true;


    // 保存标定结果
    if (!save_path.empty()) {
        save_calibration_results(save_path);
    }
    
    return true;
}

// 获取标定结果
bool StereoCalibration::get_calibration_results(cv::Mat& camera_matrix1, cv::Mat& dist_coeffs1,
                                              cv::Mat& camera_matrix2, cv::Mat& dist_coeffs2,
                                              cv::Mat& R, cv::Mat& T, 
                                              cv::Mat& E, cv::Mat& F) {
    if (!calibrated_) {
        printf("Calibration not performed yet!\n");
        return false;
    }
    
    camera_matrix1 = camera_matrix1_.clone();
    dist_coeffs1 = dist_coeffs1_.clone();
    camera_matrix2 = camera_matrix2_.clone();
    dist_coeffs2 = dist_coeffs2_.clone();
    R = R_.clone();
    T = T_.clone();
    E = E_.clone();
    F = F_.clone();
    
    return true;
}

// 加载标定结果
bool StereoCalibration::load_calibration_results(const std::string& file_path) {
    cv::FileStorage fs(file_path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        printf("Failed to open calibration file: %s\n", file_path.c_str());
        return false;
    }
    
    fs["cameraMatrix1"] >> camera_matrix1_;
    fs["distCoeffs1"] >> dist_coeffs1_;
    fs["cameraMatrix2"] >> camera_matrix2_;
    fs["distCoeffs2"] >> dist_coeffs2_;
    fs["R"] >> R_;
    fs["T"] >> T_;
    fs["E"] >> E_;
    fs["F"] >> F_;
    fs["imageSize"] >> image_size_;
    
    fs.release();
    
    calibrated_ = true;
    printf("Calibration results loaded from %s\n", file_path.c_str());
    return true;
}

// 保存标定结果
bool StereoCalibration::save_calibration_results(const std::string& file_path) {
    if (!calibrated_) {
        printf("Calibration not performed yet!\n");
        return false;
    }
    
    cv::FileStorage fs(file_path, cv::FileStorage::WRITE);
    if (!fs.isOpened()) {
        printf("Failed to create calibration file: %s\n", file_path.c_str());
        return false;
    }
    
    fs << "cameraMatrix1" << camera_matrix1_;
    fs << "distCoeffs1" << dist_coeffs1_;
    fs << "cameraMatrix2" << camera_matrix2_;
    fs << "distCoeffs2" << dist_coeffs2_;
    fs << "R" << R_;
    fs << "T" << T_;
    fs << "E" << E_;
    fs << "F" << F_;
    fs << "imageSize" << image_size_;
    
    fs.release();
    
    printf("Calibration results saved to %s\n", file_path.c_str());
    return true;
}