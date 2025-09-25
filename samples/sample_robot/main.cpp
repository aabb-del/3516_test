#include "tarkbot_r20_controlller.hpp"
#include "mpp.hpp"
#include "stereo_calibration.h"

#include <opencv2/opencv.hpp>
#include "hi_common.h"
#include "mpi_vi.h"
#include "mpi_isp.h"

#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <csignal>
#include <atomic>
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <numeric>


#include <unistd.h>
#include <stdlib.h>
#include <unistd.h> // 用于getopt
#include <stdlib.h> // 用于atoi
#include <getopt.h>

#include "image_conversion.h"

#include "video_frame_vb.hpp"
#include "stereo_match.hpp"
#include "obstacle_avoidance.hpp"
#include "debug_logger.hpp"

#include "stereo_demo.hpp"



using namespace std;



// 全局标志，用于指示程序是否需要退出
std::atomic<bool> g_should_exit(false);

// 简单的信号处理函数
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "Received SIGINT, exiting gracefully..." << std::endl;
        g_should_exit.store(true);
    }
}

// 程序模式枚举
enum OperationMode {
    MODE_CALIBRATE,
    MODE_DEPTH,
    MODE_PREVIEW,
    MODE_MATCH,
    MODE_PIC,
    MODE_UNKNOWN
};

// 帮助信息函数
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -m, --mode MODE       Set operation mode (calibrate or depth or preview or match or pic)" << std::endl;
    std::cout << "  -n, --num NUM         Number of calibration images to capture" << std::endl;
    std::cout << "  -p, --path PATH       Path to save calibration images/results" << std::endl;
    std::cout << "  -c, --config FILE     Calibration configuration file" << std::endl;
    std::cout << "  -i, --interval INT    Save interval for depth images" << std::endl;
    std::cout << "  -r, --resolution WxH  Processing resolution (e.g., 320x240)" << std::endl;
    std::cout << "  -h, --help            Display this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " -m calibrate -n 20 -p ./calib_data" << std::endl;
    std::cout << "  " << program_name << " -m depth -c calibration_result.yml -i 30" << std::endl;
}

// 解析分辨率字符串
bool parse_resolution(const std::string& resolution_str, int& width, int& height) {
    size_t x_pos = resolution_str.find('x');
    if (x_pos == std::string::npos) {
        return false;
    }
    
    width = std::atoi(resolution_str.substr(0, x_pos).c_str());
    height = std::atoi(resolution_str.substr(x_pos + 1).c_str());
    
    return width > 0 && height > 0;
}

// 解析命令行参数
bool parse_arguments(int argc, char* argv[], 
                    OperationMode& mode, 
                    int& num_calibration_images,
                    std::string& save_path,
                    std::string& config_file,
                    int& save_interval,
                    int& process_width,
                    int& process_height) {
    // 设置默认值
    mode = MODE_UNKNOWN;
    num_calibration_images = 20;
    save_path = "./calib_data";
    config_file = "./calibration_result.yml";
    save_interval = 30;
    process_width = 320;
    process_height = 240;
    
    // 定义长选项
    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"num", required_argument, 0, 'n'},
        {"path", required_argument, 0, 'p'},
        {"config", required_argument, 0, 'c'},
        {"interval", required_argument, 0, 'i'},
        {"resolution", required_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "m:n:p:c:i:r:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'm': // 模式
                if (strcmp(optarg, "calibrate") == 0) {
                    mode = MODE_CALIBRATE;
                } else if (strcmp(optarg, "depth") == 0) {
                    mode = MODE_DEPTH;
                } else if (strcmp(optarg, "preview") == 0) {
                    mode = MODE_PREVIEW; 
                } else if (strcmp(optarg, "match") == 0) {
                    mode = MODE_MATCH; 
                } else if (strcmp(optarg, "pic") == 0) {
                    mode = MODE_PIC; 
                } else {
                    std::cerr << "Unknown mode: " << optarg << std::endl;
                    return false;
                }
                break;
                
            case 'n': // 标定图像数量
                num_calibration_images = std::atoi(optarg);
                if (num_calibration_images <= 0) {
                    std::cerr << "Invalid number of calibration images: " << optarg << std::endl;
                    return false;
                }
                break;
                
            case 'p': // 保存路径
                save_path = optarg;
                break;
                
            case 'c': // 配置文件
                config_file = optarg;
                break;
                
            case 'i': // 保存间隔
                save_interval = std::atoi(optarg);
                if (save_interval <= 0) {
                    std::cerr << "Invalid save interval: " << optarg << std::endl;
                    return false;
                }
                break;
                
            case 'r': // 分辨率
                if (!parse_resolution(optarg, process_width, process_height)) {
                    std::cerr << "Invalid resolution format: " << optarg << std::endl;
                    std::cerr << "Expected format: WIDTHxHEIGHT (e.g., 320x240)" << std::endl;
                    return false;
                }
                break;
                
            case 'h': // 帮助
                print_usage(argv[0]);
                exit(0);
                
            case '?': // 未知选项
                return false;
                
            default:
                break;
        }
    }
    
    // 检查必需参数
    if (mode == MODE_UNKNOWN) {
        std::cerr << "Error: Operation mode must be specified (-m or --mode)" << std::endl;
        return false;
    }
    
    if (mode == MODE_DEPTH && access(config_file.c_str(), F_OK) == -1) {
        std::cerr << "Error: Calibration file '" << config_file << "' does not exist" << std::endl;
        std::cerr << "Please run calibration mode first or specify a valid calibration file" << std::endl;
        return false;
    }
    
    return true;
}

// 生成带时间戳的文件名
std::string generate_timestamp_filename(const std::string& prefix, const std::string& extension) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    
    // 添加毫秒
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    ss << '_' << std::setfill('0') << std::setw(3) << milliseconds.count();
    
    return prefix + "_" + ss.str() + "." + extension;
}

// 保存深度图为彩色图像
// 修改 save_depth_map 函数，添加错误检查
void save_depth_map(const cv::Mat& depth_map, const std::string& filename) {
    // 检查深度图是否为空
    if (depth_map.empty()) {
        std::cerr << "Error: depth_map is empty in save_depth_map. Cannot save: " << filename << std::endl;
        return;
    }
    
    // 检查深度图维度
    if (depth_map.dims != 2) {
        std::cerr << "Error: depth_map has invalid dimensions (" << depth_map.dims 
                  << ") in save_depth_map. Expected 2. Cannot save: " << filename << std::endl;
        return;
    }
    
    // 检查深度图数据类型
    if (depth_map.type() != CV_8UC1 && depth_map.type() != CV_16UC1 && depth_map.type() != CV_32FC1) {
        std::cerr << "Error: depth_map has unsupported type (" << depth_map.type() 
                  << ") in save_depth_map. Cannot save: " << filename << std::endl;
        return;
    }
    
    try {
        // 归一化深度图以便可视化
        cv::Mat normalized_depth;
        double min_val, max_val;
        
        // 处理特殊情况：所有值相同
        cv::minMaxLoc(depth_map, &min_val, &max_val);
        if (min_val == max_val) {
            // 所有值相同，创建一个常量图像
            normalized_depth = cv::Mat::zeros(depth_map.size(), CV_8UC1);
            std::cout << "Warning: All depth values are the same (" << min_val << ")" << std::endl;
        } else {
            // 处理无效值（如无穷大或NaN）
            cv::Mat valid_depth = depth_map.clone();
            cv::Mat mask = (depth_map != depth_map) | (depth_map > 1e6); // NaN 或无穷大
            valid_depth.setTo(0, mask);
            
            // 重新计算有效范围
            cv::minMaxLoc(valid_depth, &min_val, &max_val, 0, 0, ~mask);
            
            if (max_val > min_val) {
                valid_depth.convertTo(normalized_depth, CV_8UC1, 255.0 / (max_val - min_val), -min_val * 255.0 / (max_val - min_val));
            } else {
                normalized_depth = cv::Mat::zeros(depth_map.size(), CV_8UC1);
            }
        }
        
        // 应用颜色映射
        cv::Mat color_depth;
        cv::applyColorMap(normalized_depth, color_depth, cv::COLORMAP_JET);
        
        // 保存图像
        cv::imwrite(filename, color_depth);
        std::cout << "Saved depth map: " << filename << std::endl;
        
    } catch (const cv::Exception& e) {
        std::cerr << "OpenCV exception in save_depth_map: " << e.what() << std::endl;
        std::cerr << "Failed to save: " << filename << std::endl;
        
        // 尝试保存原始深度图（不应用颜色映射）
        try {
            cv::imwrite(filename + "_raw.png", depth_map);
            std::cout << "Saved raw depth map instead: " << filename + "_raw.png" << std::endl;
        } catch (const cv::Exception& e2) {
            std::cerr << "Also failed to save raw depth map: " << e2.what() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Standard exception in save_depth_map: " << e.what() << std::endl;
        std::cerr << "Failed to save: " << filename << std::endl;
    }
}

// 标定模式函数
int run_calibration_mode(int num_images, const std::string& save_path,
                        int process_width, int process_height) {
    std::cout << "Starting calibration mode..." << std::endl;
    std::cout << "Number of images: " << num_images << std::endl;
    std::cout << "Save path: " << save_path << std::endl;
    std::cout << "Processing resolution: " << process_width << "x" << process_height << std::endl;
    
    try {
        // 创建双目标定对象
        StereoCalibration calib(0, 0, 1, 0, 10, 7, 0.015f);
        
        // 设置退出回调函数
        calib.set_exit_callback([](){ return g_should_exit.load(); });
        
        // 采集标定图像
        int captured = calib.capture_calibration_images(num_images, save_path, true);
        if (captured < 5) {
            std::cout << "Not enough images captured! Only " << captured << " pairs." << std::endl;
            return -1;
        }
        
        // 执行双目标定
        std::string result_file = save_path + "/calibration_result.yml";
        if (calib.perform_calibration(result_file)) {
            std::cout << "Stereo calibration successful!" << std::endl;
            std::cout << "Results saved to: " << result_file << std::endl;
            
            // 获取并显示标定结果
            cv::Mat camera_matrix1, dist_coeffs1;
            cv::Mat camera_matrix2, dist_coeffs2;
            cv::Mat R, T, E, F;
            
            if (calib.get_calibration_results(camera_matrix1, dist_coeffs1,
                                            camera_matrix2, dist_coeffs2,
                                            R, T, E, F)) {
                std::cout << "Left camera matrix:\n" << camera_matrix1 << std::endl;
                std::cout << "Right camera matrix:\n" << camera_matrix2 << std::endl;
                std::cout << "Rotation matrix:\n" << R << std::endl;
                std::cout << "Translation vector:\n" << T << std::endl;
            }
            
            return 0;
        } else {
            std::cout << "Stereo calibration failed!" << std::endl;
            return -1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in calibration mode: " << e.what() << std::endl;
        return -1;
    }
}


// 添加调试函数
void debug_mat_info(const cv::Mat& mat, const std::string& name) {
    std::cout << name << " info:" << std::endl;
    std::cout << "  Empty: " << (mat.empty() ? "Yes" : "No") << std::endl;
    std::cout << "  Dimensions: " << mat.dims << std::endl;
    std::cout << "  Size: " << mat.size() << std::endl;
    std::cout << "  Type: " << mat.type() << std::endl;
    std::cout << "  Channels: " << mat.channels() << std::endl;
    
    if (!mat.empty()) {
        double min_val, max_val;
        cv::minMaxLoc(mat, &min_val, &max_val);
        std::cout << "  Min value: " << min_val << std::endl;
        std::cout << "  Max value: " << max_val << std::endl;
    }
    std::cout << std::endl;
}

// 在 compute_depth_map 函数中添加调试信息
bool compute_depth_map(const cv::Mat& disparity, const cv::Mat& Q, cv::Mat& depth_map_16u) {
    try {
        debug_mat_info(disparity, "Disparity");
        
        // 检查视差图是否有效
        if (disparity.empty() || disparity.channels() != 1) {
            std::cerr << "Error: Invalid disparity map" << std::endl;
            return false;
        }
        
        // 处理负视差值
        cv::Mat disparity_processed = disparity.clone();
        disparity_processed.setTo(0, disparity < 0);
        
        // 计算深度图
        cv::Mat depth_map_32f;
        cv::reprojectImageTo3D(disparity_processed, depth_map_32f, Q, true);
        
        debug_mat_info(depth_map_32f, "Depth Map 32F");
        
        // 检查深度图是否有效
        if (depth_map_32f.empty() || depth_map_32f.channels() != 3) {
            std::cerr << "Error: Invalid depth map" << std::endl;
            return false;
        }
        
        // 提取深度值通道（Z通道）
        cv::Mat depth_z;
        cv::extractChannel(depth_map_32f, depth_z, 2);
        
        debug_mat_info(depth_z, "Depth Z");
        
        // 检查深度值通道是否有效
        if (depth_z.empty()) {
            std::cerr << "Error: Failed to extract depth channel" << std::endl;
            return false;
        }
        
        // 创建掩码 - 处理无穷大值
        cv::Mat inf_mask = (depth_z > 1e6);
        
        // 创建无效视差掩码（原始视差为负或0的位置）
        cv::Mat invalid_disp_mask = (disparity <= 0);
        
        // 合并所有无效掩码
        cv::Mat invalid_mask = inf_mask | invalid_disp_mask;
        
        debug_mat_info(invalid_mask, "Invalid Mask");
        
        // 计算有效像素数量
        int valid_pixels = depth_z.total() - cv::countNonZero(invalid_mask);
        
        std::cout << "Total pixels: " << depth_z.total() << std::endl;
        std::cout << "Invalid pixels: " << cv::countNonZero(invalid_mask) << std::endl;
        std::cout << "Valid pixels: " << valid_pixels << std::endl;
        
        if (valid_pixels == 0) {
            std::cerr << "Warning: No valid depth values in depth map" << std::endl;
            return false;
        }
        
        // 将无效深度值设置为0
        cv::Mat depth_z_clean = depth_z.clone();
        depth_z_clean.setTo(0, invalid_mask);
        
        // 转换为16位无符号整数
        depth_z_clean.convertTo(depth_map_16u, CV_16UC1, 1000.0); // 米转换为毫米
        
        debug_mat_info(depth_map_16u, "Depth Map 16U");
        
        return true;
        
    } catch (const cv::Exception& e) {
        std::cerr << "OpenCV exception in compute_depth_map: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Standard exception in compute_depth_map: " << e.what() << std::endl;
        return false;
    }
}


// 添加深度图后处理函数
void postprocess_depth_map(cv::Mat& depth_map) {
    // 应用中值滤波去除噪声
    cv::medianBlur(depth_map, depth_map, 5);
    
    // 可选：使用形态学操作填充小洞
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(depth_map, depth_map, cv::MORPH_CLOSE, kernel);
    
    // 可选：使用双边滤波保持边缘
    // cv::bilateralFilter(depth_map, depth_map, 5, 50, 50);
}


// 添加深度图验证函数
bool validate_depth_map(const cv::Mat& depth_map, float min_valid_depth = 0.1f, float max_valid_depth = 10.0f) {
    if (depth_map.empty()) {
        std::cerr << "Error: Depth map is empty" << std::endl;
        return false;
    }
    
    // 检查深度值范围
    double min_val, max_val;
    cv::minMaxLoc(depth_map, &min_val, &max_val);
    
    std::cout << "Depth range: " << min_val << " to " << max_val << " meters" << std::endl;
    
    // 检查是否有合理数量的有效深度值
    cv::Mat valid_mask = (depth_map >= min_valid_depth) & (depth_map <= max_valid_depth);
    int valid_pixels = cv::countNonZero(valid_mask);
    float valid_ratio = static_cast<float>(valid_pixels) / depth_map.total();
    
    std::cout << "Valid pixels: " << valid_pixels << "/" << depth_map.total() 
              << " (" << valid_ratio * 100.0f << "%)" << std::endl;
    
    // 如果有效像素比例太低，认为深度图无效
    if (valid_ratio < 0.1f) { // 10%阈值
        std::cerr << "Warning: Too few valid depth values (" << valid_ratio * 100.0f << "%)" << std::endl;
        return false;
    }
    
    return true;
}



// 深度计算模式函数
int run_depth_mode(const std::string& config_file, int save_interval,
                  int process_width, int process_height) {
    std::cout << "Starting depth calculation mode..." << std::endl;
    std::cout << "Calibration file: " << config_file << std::endl;
    std::cout << "Save interval: " << save_interval << std::endl;
    std::cout << "Processing resolution: " << process_width << "x" << process_height << std::endl;
    
    // 加载标定结果
    StereoCalibration calib(0, 0, 1, 0, 10, 7, 0.015f);
    if (!calib.load_calibration_results(config_file)) {
        std::cerr << "Failed to load calibration results from: " << config_file << std::endl;
        return -1;
    }
    
    // 获取标定参数
    cv::Mat camera_matrix1, dist_coeffs1;
    cv::Mat camera_matrix2, dist_coeffs2;
    cv::Mat R, T, E, F;
    cv::Size image_size;
    
    if (!calib.get_calibration_results(camera_matrix1, dist_coeffs1,
                                     camera_matrix2, dist_coeffs2,
                                     R, T, E, F)) {
        std::cerr << "Failed to get calibration results!" << std::endl;
        return -1;
    }
    
    // 获取图像尺寸
    // 注意：这里需要从标定结果中获取实际的图像尺寸
    // 如果标定结果中没有保存图像尺寸，可能需要从相机获取
    // 这里假设标定使用的是原始图像尺寸
    image_size = cv::Size(1920, 1080); // 根据实际情况修改
    
    std::cout << "Loaded calibration results successfully!" << std::endl;
    std::cout << "Image size: " << image_size.width << "x" << image_size.height << std::endl;
    
    // 计算立体校正参数
    cv::Mat R1, R2, P1, P2, Q;
    cv::stereoRectify(camera_matrix1, dist_coeffs1,
                     camera_matrix2, dist_coeffs2,
                     image_size, R, T, R1, R2, P1, P2, Q,
                     cv::CALIB_ZERO_DISPARITY, 0, image_size);
    
    // 计算校正映射
    cv::Mat left_map1, left_map2;
    cv::Mat right_map1, right_map2;
    
    cv::initUndistortRectifyMap(camera_matrix1, dist_coeffs1, R1, P1,
                               image_size, CV_16SC2, left_map1, left_map2);
    
    cv::initUndistortRectifyMap(camera_matrix2, dist_coeffs2, R2, P2,
                               image_size, CV_16SC2, right_map1, right_map2);
    
    // 创建立体匹配器
    cv::Ptr<cv::StereoBM> stereo = cv::StereoBM::create(16, 15);
    
    // 创建保存目录
    system("mkdir -p output_images");
    
    // 主循环
    HI_S32 s32Ret;
    int camera_num = 2;
    int frame_count = 0;
    
    std::cout << "Starting depth calculation loop. Press Ctrl+C to exit." << std::endl;
    
    while(!g_should_exit.load()) {
        VIFrameRAII left_frame(0, 0);
        VIFrameRAII right_frame(1, 0);
        
        if (!left_frame.acquire() || !right_frame.acquire()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // 送到显示屏显示
        s32Ret = HI_MPI_VO_SendFrame(0, 0, left_frame.get(), -1);
        s32Ret = HI_MPI_VO_SendFrame(0, 1, right_frame.get(), -1);


        // 转换为OpenCV Mat
        cv::Mat left_img, right_img;
        if (!calib.yuv_to_bgr(*(left_frame.get()), left_img) || 
            !calib.yuv_to_bgr(*(right_frame.get()), right_img)) {
            std::cout << "Failed to convert YUV to BGR" << std::endl;
            continue;
        }
        
        // 应用立体校正
        cv::Mat left_rectified, right_rectified;
        cv::remap(left_img, left_rectified, left_map1, left_map2, cv::INTER_LINEAR);
        cv::remap(right_img, right_rectified, right_map1, right_map2, cv::INTER_LINEAR);
        
        // 立即释放原始图像内存
        left_img.release();
        right_img.release();
        
        
        // 降低分辨率进行处理
        cv::Mat left_small, right_small;
        cv::resize(left_rectified, left_small, cv::Size(process_width, process_height), 0, 0, cv::INTER_AREA);
        cv::resize(right_rectified, right_small, cv::Size(process_width, process_height), 0, 0, cv::INTER_AREA);
        
        // 立即释放校正后的图像内存
        left_rectified.release();
        right_rectified.release();
        
        // 转换为灰度图
        cv::Mat left_gray, right_gray;
        cv::cvtColor(left_small, left_gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right_small, right_gray, cv::COLOR_BGR2GRAY);
        
        // 立即释放小彩色图像内存
        left_small.release();
        right_small.release();
        
        // 计算视差图
        cv::Mat disparity_small;
        stereo->compute(left_gray, right_gray, disparity_small);
        
        // 立即释放灰度图内存
        left_gray.release();
        right_gray.release();
        
        // 上采样视差图到原始分辨率
        cv::Mat disparity;
        cv::resize(disparity_small, disparity, image_size, 0, 0, cv::INTER_NEAREST);
        
        // 立即释放小视差图内存
        disparity_small.release();
        
        // 计算深度图
        cv::Mat depth_z_16u;
        if (compute_depth_map(disparity, Q, depth_z_16u)) {
            // 验证深度图
            if (validate_depth_map(depth_z_16u, 0.1f, 10.0f)) {
                // 后处理深度图
                postprocess_depth_map(depth_z_16u);
                
                // 保存深度图
                if (frame_count % save_interval == 0) {
                    std::string timestamp = generate_timestamp_filename("output_images/frame", "");
                    save_depth_map(depth_z_16u, timestamp + "_depth.png");
                }
            } else {
                std::cerr << "Depth map validation failed for frame " << frame_count << std::endl;
            }
            
            // 立即释放深度图内存
            depth_z_16u.release();
        } else {
            std::cerr << "Failed to compute depth map for frame " << frame_count << std::endl;
        }
        
        frame_count++;
        
        if (g_should_exit.load()) {
            break;
        }
    }
    
    std::cout << "Depth calculation mode exited gracefully." << std::endl;
    return 0;
}





void preview_video_and_save()
{
    int left_pipe = 0;
    int left_chn = 0;
    int right_pipe = 1;
    int right_chn = 0;
    int index = 0;

    char user_input = '\0';
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> save_flag{false};
    
    // 启动匿名线程监控用户输入
    std::thread([&]() {
        while (!stop_flag) {
            std::cout << "请输入一个字符 (输入'q'退出，输入's'保存): ";
            std::cin >> user_input;
            
            // 根据输入执行操作
            if (user_input == 'q') {
                stop_flag = true;
                std::cout << "退出程序..." << std::endl;
            } else {
                std::cout << "您输入的是: " << user_input << std::endl;
                // 这里可以添加更多基于输入的操作
                if(user_input == 's') {
                    save_flag = true;
                }
            }
        }
    }).detach(); // 分离线程，使其在后台运行



    while(!g_should_exit.load())
    {
        std::unique_ptr<VIFrameRAII> left_frame;
        std::unique_ptr<VIFrameRAII> right_frame;

        left_frame = std::make_unique<VIFrameRAII>(left_pipe, left_chn);
        right_frame = std::make_unique<VIFrameRAII>(right_pipe, right_chn);
        
        if((!left_frame->acquire()) || (!right_frame->acquire()))
        {
            continue;
        }


        if(save_flag)
        {
            index++;
            ImageConversion::CPU::save_vi_frame_as_jpg(*(left_frame->get()), "left_" + std::to_string(index) + ".jpg");
            ImageConversion::CPU::save_vi_frame_as_jpg(*(right_frame->get()), "right_" + std::to_string(index) + ".jpg");

            sync();

            std::cout << "save " << index << " image" << endl;
            save_flag = false;
        }



        HI_MPI_VO_SendFrame(0, 0, left_frame->get(), -1);
        HI_MPI_VO_SendFrame(0, 1, right_frame->get(), -1);
    }
}




void frame_vb_test()
{
    // 创建VB帧对象（自动申请资源）
    VideoFrameVB frame(1920, 1080);

    // 获取帧信息
    VIDEO_FRAME_INFO_S* pstFrameInfo = frame.getFrameInfo();
    int index = 0;


    while(!g_should_exit.load())
    {

        // 方法1：手动标记修改并刷新缓存
        // 修改数据
        uint8_t* addr = (uint8_t*)pstFrameInfo->stVFrame.u64VirAddr[0];
        for(int i=0; i< 1920*1080 ;i++)
        {
            addr[i] = index;
        }

        for(int i=(1920*1080); i< (1920*1080*2) ;i++)
        {
            addr[i] = 0x80;
        }
        frame.markDirty(); // 标记数据已修改
        frame.flushCache(); // 刷新缓存，确保数据同步到物理内存


        HI_MPI_VO_SendFrame(0, 0, pstFrameInfo, -1);



        // 方法2：使用RAII方式自动刷新缓存
        frame.withCacheFlush([&]() {
            // 在这个lambda中修改数据
            uint8_t* addr = (uint8_t*)pstFrameInfo->stVFrame.u64VirAddr[0];
            for(int i=0; i< 1920*1080 ;i++)
            {
                addr[i] = index;
            }

            for(int i=(1920*1080); i< (1920*1080*2) ;i++)
            {
                addr[i] = 0x80;
            }
            return 0; // 返回任意值
        });
        
        index++;

        HI_MPI_VO_SendFrame(0, 1, pstFrameInfo, -1);

    }

}



// 主函数
int main(int argc, char* argv[]) {
    // 注册信号处理函数
    std::signal(SIGINT, signal_handler);
    
    // 解析命令行参数
    OperationMode mode;
    int num_calibration_images;
    std::string save_path;
    std::string config_file;
    int save_interval;
    int process_width;
    int process_height;
    
    if (!parse_arguments(argc, argv, mode, num_calibration_images, save_path, 
                        config_file, save_interval, process_width, process_height)) {
        print_usage(argv[0]);
        return -1;
    }
    
    // 初始化MPP
    Mpp mpp;
    mpp.vi_init();
    mpp.vo_init();



    // 根据模式执行不同的操作
    if (mode == MODE_CALIBRATE) {
        return run_calibration_mode(num_calibration_images, save_path, 
                                  process_width, process_height);
    } else if (mode == MODE_DEPTH) {
        return run_depth_mode(config_file, save_interval, 
                            process_width, process_height);
    } else if(mode == MODE_PREVIEW)
    {
        preview_video_and_save();
    } else if(mode == MODE_MATCH)
    {
        stereo_matcher_test_hi();
    } else if(mode == MODE_PIC)
    {
        stereo_matcher_test();
    }


    

    
    return 0;
}