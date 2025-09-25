#ifndef STEREO_CALIBRATION_HPP
#define STEREO_CALIBRATION_HPP

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <future>
#include <atomic>

#include <opencv2/opencv.hpp>
#include "hi_comm_vi.h"
#include "mpi_vi.h"
#include "hi_comm_sys.h"
#include "hi_common.h"
#include "hi_comm_video.h"

#include "raii_wrappers.hpp"

class StereoCalibration {
public:
    /**
     * @brief 构造函数
     * @param left_pipe 左相机pipe号
     * @param left_chn 左相机通道号
     * @param right_pipe 右相机pipe号
     * @param right_chn 右相机通道号
     * @param board_width 标定板角点宽度数量
     * @param board_height 标定板角点高度数量
     * @param square_size 标定板方格大小(单位:米)
     */
    StereoCalibration(int left_pipe, int left_chn, 
                     int right_pipe, int right_chn,
                     int board_width, int board_height, 
                     float square_size);
    
    /**
     * @brief 析构函数
     */
    ~StereoCalibration();
    
    /**
     * @brief 采集标定图像
     * @param num_frames 需要采集的图像数量
     * @param save_path 图像保存路径
     * @return 成功采集的图像数量
     */
    int capture_calibration_images(int num_frames, const std::string& save_path = "", bool use_y_channel_only = true);


    /**
     * @brief 执行双目标定
     * @param save_path 标定结果保存路径
     * @return 标定是否成功
     */
    bool perform_calibration(const std::string& save_path = "");
    
    /**
     * @brief 获取标定结果
     * @param camera_matrix1 左相机内参矩阵
     * @param dist_coeffs1 左相机畸变系数
     * @param camera_matrix2 右相机内参矩阵
     * @param dist_coeffs2 右相机畸变系数
     * @param R 旋转矩阵
     * @param T 平移向量
     * @param E 本质矩阵
     * @param F 基础矩阵
     * @return 是否成功获取标定结果
     */
    bool get_calibration_results(cv::Mat& camera_matrix1, cv::Mat& dist_coeffs1,
                                cv::Mat& camera_matrix2, cv::Mat& dist_coeffs2,
                                cv::Mat& R, cv::Mat& T, 
                                cv::Mat& E, cv::Mat& F);
    
    /**
     * @brief 加载标定结果
     * @param file_path 标定文件路径
     * @return 是否成功加载
     */
    bool load_calibration_results(const std::string& file_path);
    
    /**
     * @brief 保存标定结果
     * @param file_path 标定文件路径
     * @return 是否成功保存
     */
    bool save_calibration_results(const std::string& file_path);


    /**
     * @brief 设置退出标志
     * @param should_exit 是否应该退出
     */
    void set_should_exit(bool should_exit) { should_exit_ = should_exit; }
    
    /**
     * @brief 检查是否应该退出
     * @return 是否应该退出
     */
    bool should_exit() const { return should_exit_; }


    /**
     * @brief 设置退出检查回调函数
     * @param callback 退出检查回调函数，返回true表示应该退出
     */
    void set_exit_callback(std::function<bool()> callback) { exit_callback_ = callback; }


        /**
     * @brief 转换YUV图像到BGR格式
     * @param yuv_frame YUV图像帧
     * @param bgr_img 输出BGR图像
     * @return 是否成功转换
     */
    bool yuv_to_bgr(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& bgr_img);


    bool yuv_to_gray(const VIDEO_FRAME_INFO_S& yuv_frame, cv::Mat& gray_img);

    
private:
    /**
     * @brief 从VI通道获取图像
     * @param pipe VI Pipe号
     * @param chn VI通道号
     * @param frame 输出图像帧
     * @return 是否成功获取
     */
    bool get_frame_from_vi(int pipe, int chn, std::unique_ptr<VIFrameRAII>& frame_raii);
    

    
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
     * @brief 检测棋盘格角点
     * @param img 输入图像
     * @param corners 输出角点坐标
     * @return 是否成功检测到角点
     */
    bool find_chessboard_corners(const cv::Mat& img, std::vector<cv::Point2f>& corners);
    bool find_chessboard_corners_async(const cv::Mat& img, std::vector<cv::Point2f>& corners);
    
    int left_pipe_;          // 左相机pipe号
    int left_chn_;           // 左相机通道号
    int right_pipe_;         // 右相机pipe号
    int right_chn_;          // 右相机通道号
    
    int board_width_;        // 标定板角点宽度数量
    int board_height_;       // 标定板角点高度数量
    float square_size_;      // 标定板方格大小(米)
    
    cv::Size image_size_;    // 图像尺寸
    
    // 标定结果
    cv::Mat camera_matrix1_, dist_coeffs1_;  // 左相机内参和畸变
    cv::Mat camera_matrix2_, dist_coeffs2_;  // 右相机内参和畸变
    cv::Mat R_, T_, E_, F_;                  // 外参和基本矩阵
    
    // 标定数据
    std::vector<std::vector<cv::Point2f>> left_image_points_;   // 左图像角点
    std::vector<std::vector<cv::Point2f>> right_image_points_;  // 右图像角点
    std::vector<std::vector<cv::Point3f>> object_points_;       // 世界坐标系角点
    
    bool calibrated_;        // 标定完成标志

    bool should_exit_;  // 退出标志
    std::function<bool()> exit_callback_;

    std::future<bool> left_detection_future;
    std::future<bool> right_detection_future;
    std::atomic<bool> detection_cancelled{false};
};

#endif // STEREO_CALIBRATION_HPP