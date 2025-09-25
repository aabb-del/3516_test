#include "obstacle_avoidance.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

ObstacleAvoidance::ObstacleAvoidance(const CameraParameters& cam_params, 
                                   const ObstacleAvoidanceConfig& config) 
    : camera_params_(cam_params), config_(config) {
    // 初始化代价地图
    costmap_ = cv::Mat::zeros(config_.grid_height, config_.grid_width, CV_8UC1);
}

ObstacleAvoidance::~ObstacleAvoidance() {
    // 清理资源
}

void ObstacleAvoidance::setConfig(const ObstacleAvoidanceConfig& config) {
    config_ = config;
    // 重置代价地图
    costmap_ = cv::Mat::zeros(config_.grid_height, config_.grid_width, CV_8UC1);
}

ObstacleAvoidanceConfig ObstacleAvoidance::getConfig() const {
    return config_;
}

void ObstacleAvoidance::setCameraParameters(const CameraParameters& cam_params) {
    camera_params_ = cam_params;
}

CameraParameters ObstacleAvoidance::getCameraParameters() const {
    return camera_params_;
}



NavigationCommand ObstacleAvoidance::processDisparityMap(const cv::Mat& disparity,  const cv::Mat& Q) {
    // 保存视差图用于可视化
    last_disparity_map_ = disparity.clone();
    
    // 从视差图创建代价地图
    costmap_ = createCostMapFromDisparity(disparity, Q);
    
    // 寻找安全路径
    return findSafePath(costmap_);
}


// Q矩阵需要从外部获取
cv::Mat ObstacleAvoidance::createCostMapFromDisparity(const cv::Mat& disparity, const cv::Mat& Q) {
    cv::Mat costmap = cv::Mat::zeros(config_.grid_height, config_.grid_width, CV_8UC1);
    
    // 添加调试信息
    std::cout << "Disparity map info: " 
              << "size=" << disparity.size() 
              << ", type=" << disparity.type() 
              << ", channels=" << disparity.channels() << std::endl;
    
    double min_val, max_val;
    cv::minMaxLoc(disparity, &min_val, &max_val);
    std::cout << "Disparity range: " << min_val << " to " << max_val << std::endl;
    
    // 检查视差图有效性
    if (disparity.empty() || (disparity.type() != CV_32FC1 && disparity.type() != CV_16SC1)) {
        std::cerr << "Invalid disparity map format" << std::endl;
        return costmap;
    }
    
    // 检查相机参数有效性
    if (!camera_params_.isValid()) {
        std::cerr << "Camera parameters are not valid for disparity processing" << std::endl;
        return costmap;
    }
    

    if (Q.empty() || Q.rows != 4 || Q.cols != 4) {
        std::cerr << "Invalid Q matrix from camera parameters" << std::endl;
        return costmap;
    }
    
    // 打印Q矩阵信息
    std::cout << "Q matrix: " << std::endl << Q << std::endl;
    
    int valid_points = 0;
    int obstacle_points = 0;


    
    // 遍历视差图的每个像素
    for (int v = 0; v < disparity.rows; ++v) {
        for (int u = 0; u < disparity.cols; ++u) {
            // 获取视差值
            float disp_val =  disparity.at<float>(v, u);;
            if (disparity.type() == CV_16SC1) {
                disp_val = static_cast<float>(disparity.at<short>(v, u)) / 16.0f;
            } else {
                disp_val = disparity.at<float>(v, u);
            }

            // 跳过无效视差值
            if (disp_val <= 0) {
                continue;
            }
            
            valid_points++;
            
            // 计算深度
            // float depth = (Q.at<float>(3, 2) / disp_val) + Q.at<float>(3, 3);
            float depth = 5864.73 / disp_val;

            double test = -1.0 * (double)Q.at<float>(2, 3) / ((double)disp_val  * (double)Q.at<float>(3, 2));
            // float depth = (Q.at<float>(2, 3) * Q.at<float>(3, 2)) / disp_val;
            // double depth = 

            if(depth != 0 )
            {
                std::cout << "disp_val " << disp_val << std::endl;
                std::cout << "test " << test << std::endl;
            }

            // // 跳过无效深度值
            // if (depth <= config_.min_detection_range || depth >= config_.max_detection_range) {
            //     continue;
            // }
            
            // 将图像坐标转换为世界坐标
            float x, y, z;
            disparityToWorld(u, v, disp_val, Q, x, y, z);
            
            

            // 跳过高度不在障碍物范围内的点
            if (z < config_.min_obstacle_height || z > config_.max_obstacle_height) {
                continue;
            }
            
            // 将世界坐标转换为栅格坐标
            int grid_x, grid_y;
            if (worldToGrid(x, y, grid_x, grid_y)) {
                // 标记为障碍物
                costmap.at<uchar>(grid_y, grid_x) = 100;
                obstacle_points++;
            }
        }
    }
    
    // 打印统计信息
    std::cout << "Valid points: " << valid_points 
              << ", Obstacle points: " << obstacle_points 
              << ", Total pixels: " << disparity.rows * disparity.cols << std::endl;
    
    // 膨胀障碍物
    inflateObstacles(costmap, static_cast<int>((config_.robot_radius + config_.safety_margin) * config_.grid_resolution));
    
    return costmap;
}


void ObstacleAvoidance::disparityToWorld(float u, float v, float disparity, const cv::Mat& Q, 
                                        float& x, float& y, float& z) const {
    // 检查视差值是否有效
    if (disparity <= 0 || !std::isfinite(disparity) || disparity > 1000) {
        x = y = z = 0;
        return;
    }
    
    // 使用double进行计算，避免精度损失和溢出
    double w = static_cast<double>(Q.at<float>(3, 2)) * 
               static_cast<double>(disparity) + 
               static_cast<double>(Q.at<float>(3, 3));
    
    // 检查w值是否有效
    if (!std::isfinite(w) || fabs(w) < 1e-10) {
        x = y = z = 0;
        return;
    }
    
    // 计算分子
    double z_num = static_cast<double>(Q.at<float>(2, 0)) * u + 
                   static_cast<double>(Q.at<float>(2, 1)) * v + 
                   static_cast<double>(Q.at<float>(2, 2)) * disparity + 
                   static_cast<double>(Q.at<float>(2, 3));
    
    double x_num = static_cast<double>(Q.at<float>(0, 0)) * u + 
                   static_cast<double>(Q.at<float>(0, 1)) * v + 
                   static_cast<double>(Q.at<float>(0, 2)) * disparity + 
                   static_cast<double>(Q.at<float>(0, 3));
    
    double y_num = static_cast<double>(Q.at<float>(1, 0)) * u + 
                   static_cast<double>(Q.at<float>(1, 1)) * v + 
                   static_cast<double>(Q.at<float>(1, 2)) * disparity + 
                   static_cast<double>(Q.at<float>(1, 3));
    
    // 计算3D坐标
    z = static_cast<float>(z_num / w);
    x = static_cast<float>(x_num / w);
    y = static_cast<float>(y_num / w);
    
    // 检查计算结果是否有效
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        x = y = z = 0;
        return;
    }
    
    // 考虑相机高度（单位：厘米）
    y += config_.camera_height;
    
    // 考虑相机俯仰角
    if (config_.camera_pitch != 0.0f) {
        float cos_pitch = cos(config_.camera_pitch);
        float sin_pitch = sin(config_.camera_pitch);
        float new_y = y * cos_pitch - z * sin_pitch;
        z = y * sin_pitch + z * cos_pitch;
        y = new_y;
        
        // 再次检查计算结果
        if (!std::isfinite(y) || !std::isfinite(z)) {
            x = y = z = 0;
            return;
        }
    }
}

cv::Mat ObstacleAvoidance::visualizeDisparityMap(const cv::Mat& disparity) const {
    if (disparity.empty()) {
        return cv::Mat();
    }
    
    // 归一化视差图以便可视化
    cv::Mat normalized_disp;
    double min_val, max_val;
    cv::minMaxLoc(disparity, &min_val, &max_val);
    
    if (min_val == max_val) {
        normalized_disp = cv::Mat::zeros(disparity.size(), CV_8UC1);
    } else {
        // 对于16位有符号视差图，需要特殊处理
        if (disparity.type() == CV_16SC1) {
            cv::Mat disp_float;
            disparity.convertTo(disp_float, CV_32F, 1.0 / 16.0);
            cv::minMaxLoc(disp_float, &min_val, &max_val);
            disp_float.convertTo(normalized_disp, CV_8UC1, 255.0 / (max_val - min_val), -min_val * 255.0 / (max_val - min_val));
        } else {
            disparity.convertTo(normalized_disp, CV_8UC1, 255.0 / (max_val - min_val), -min_val * 255.0 / (max_val - min_val));
        }
    }
    
    // 应用颜色映射
    cv::Mat color_disp;
    cv::applyColorMap(normalized_disp, color_disp, cv::COLORMAP_JET);
    
    return color_disp;
}


NavigationCommand ObstacleAvoidance::processDepthMap(const cv::Mat& depth_map) {
    // 保存深度图用于可视化
    last_depth_map_ = depth_map.clone();
    
    // 从深度图创建代价地图
    costmap_ = createCostMapFromDepth(depth_map);
    
    // 寻找安全路径
    return findSafePath(costmap_);
}

NavigationCommand ObstacleAvoidance::processPointCloud(const cv::Mat& point_cloud) {
    // 从点云创建代价地图
    costmap_ = createCostMapFromPointCloud(point_cloud);
    
    // 寻找安全路径
    return findSafePath(costmap_);
}

cv::Mat ObstacleAvoidance::createCostMapFromDepth(const cv::Mat& depth_map) {
    cv::Mat costmap = cv::Mat::zeros(config_.grid_height, config_.grid_width, CV_8UC1);
    
    // 检查深度图有效性
    if (depth_map.empty() || depth_map.type() != CV_32FC1) {
        std::cerr << "Invalid depth map format" << std::endl;
        return costmap;
    }
    
    // 检查相机参数有效性
    if (!camera_params_.isValid()) {
        std::cerr << "Camera parameters are not valid for depth map processing" << std::endl;
        return costmap;
    }
    
    // 获取相机参数
    cv::Mat camera_matrix = camera_params_.getCameraMatrix1();
    cv::Mat dist_coeffs = camera_params_.getDistCoeffs1();
    
    // 遍历深度图的每个像素
    for (int v = 0; v < depth_map.rows; ++v) {
        for (int u = 0; u < depth_map.cols; ++u) {
            float depth = depth_map.at<float>(v, u);
            
            // 跳过无效深度值
            if (depth <= config_.min_detection_range || depth >= config_.max_detection_range) {
                continue;
            }
            
            // 将图像坐标转换为世界坐标
            float x, y, z;
            imageToWorld(u, v, depth, x, y, z);
            
            // 跳过高度不在障碍物范围内的点
            if (z < config_.min_obstacle_height || z > config_.max_obstacle_height) {
                continue;
            }
            
            // 将世界坐标转换为栅格坐标
            int grid_x, grid_y;
            if (worldToGrid(x, y, grid_x, grid_y)) {
                // 标记为障碍物
                costmap.at<uchar>(grid_y, grid_x) = 100;
            }
        }
    }
    
    // 膨胀障碍物
    inflateObstacles(costmap, static_cast<int>((config_.robot_radius + config_.safety_margin) * config_.grid_resolution));
    
    return costmap;
}


cv::Vec3f ObstacleAvoidance::transformCameraToRobot(const cv::Vec3f& point_cam) const {
    // 假设相机安装在机器人前方，高度为config_.camera_height
    // 并且相机光轴与地面平行（无俯仰角）
    
    cv::Vec3f point_robot;
    
    // 相机坐标到机器人坐标的转换
    // 注意：这取决于相机的安装方式
    point_robot[0] = point_cam[0];  // X: 右侧方向不变
    point_robot[1] = point_cam[2];  // Y: 相机前方变为机器人前方
    point_robot[2] = -point_cam[1] + config_.camera_height; // Z: 相机下方变为机器人上方，并考虑相机高度
    
    return point_robot;
}


cv::Mat ObstacleAvoidance::createCostMapFromPointCloud(const cv::Mat& point_cloud) {
    cv::Mat costmap = cv::Mat::zeros(config_.grid_height, config_.grid_width, CV_8UC1);
    
    // 检查点云有效性
    if (point_cloud.empty() || point_cloud.type() != CV_32FC3) {
        std::cerr << "Invalid point cloud format" << std::endl;
        return costmap;
    }
    
    // 遍历点云的每个点
    for (int v = 0; v < point_cloud.rows; ++v) {
        for (int u = 0; u < point_cloud.cols; ++u) {
            // 将点从相机坐标系转换到机器人坐标系
            cv::Vec3f point_cam = point_cloud.at<cv::Vec3f>(v, u);
            cv::Vec3f point_robot = transformCameraToRobot(point_cam);
            
            float x = point_robot[0]; // 机器人右侧
            float y = point_robot[1]; // 机器人前方
            float z = point_robot[2]; // 机器人上方


            
            // 跳过太近或太远的点（基于前方距离）
            if (y <= config_.min_detection_range || y >= config_.max_detection_range) {
                continue;
            }
            
            // 跳过高度不在障碍物范围内的点
            if (z < config_.min_obstacle_height || z > config_.max_obstacle_height) {
                continue;
            }
            
            // // 调试输出
            // std::cout << "x=" << x << ", y=" << y 
            //         << " z=" << z  << std::endl;

            // 将世界坐标转换为栅格坐标
            int grid_x, grid_y;
            if (worldToGrid(x, y, grid_x, grid_y)) {
                // 标记为障碍物
                costmap.at<uchar>(grid_y, grid_x) = 100;
            }
        }
    }
    
    // 膨胀障碍物
    inflateObstacles(costmap, static_cast<int>((config_.robot_radius + config_.safety_margin) * config_.grid_resolution));
    
    return costmap;
}

void ObstacleAvoidance::inflateObstacles(cv::Mat& costmap, int inflation_radius) {
    if (inflation_radius <= 0) {
        return;
    }
    
    // 创建膨胀核
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, 
                                              cv::Size(2 * inflation_radius + 1, 2 * inflation_radius + 1));
    
    // 膨胀操作
    cv::dilate(costmap, costmap, kernel);
}

NavigationCommand ObstacleAvoidance::findSafePath(const cv::Mat& costmap) {
    NavigationCommand cmd;
    
    // 检查代价地图有效性
    if (costmap.empty()) {
        cmd.stop = true;
        cmd.debug_info = "Empty costmap";
        return cmd;
    }
    
    // 机器人位置在栅格地图的底部中心
    const int robot_x = config_.grid_width / 2;
    const int robot_y = config_.grid_height - 1;
    const int max_distance = static_cast<int>(config_.max_detection_range * config_.grid_resolution);
    
    // 寻找最安全的路径（最大间隙）
    int best_direction = 0;
    int max_gap = 0;
    
    // 扫描前方扇形区域 (±45度)
    for (int angle = -45; angle <= 45; angle += 5) {
        int gap_size = 0;
        
        // 计算当前角度的射线
        float rad = angle * CV_PI / 180.0f;
        for (int r = 1; r <= max_distance; r++) {
            int x = robot_x + static_cast<int>(r * sin(rad));
            int y = robot_y - static_cast<int>(r * cos(rad));  // 从机器人位置向前扫描
            
            if (x < 0 || x >= config_.grid_width || y < 0 || y >= config_.grid_height) {
                break;
            }
            
            // 检查是否是障碍物
            if (costmap.at<uchar>(y, x) > 0) {
                break;
            }
            
            gap_size++;
        }
        
        // 更新最佳方向
        if (gap_size > max_gap) {
            max_gap = gap_size;
            best_direction = angle;
        }
    }
    
    // 根据找到的最佳方向生成控制指令
    if (max_gap > 0) {
        // 有安全路径
        cmd.linear_velocity = config_.max_speed * (static_cast<float>(max_gap) / max_distance);
        cmd.angular_velocity = -best_direction * CV_PI / 180.0f * config_.max_turn_rate;
        cmd.recommended_direction = best_direction * CV_PI / 180.0f;
        cmd.stop = false;
        cmd.debug_info = "Safe path found: gap=" + std::to_string(max_gap) + 
                         ", direction=" + std::to_string(best_direction) + " deg";
    } else {
        // 没有安全路径，停止
        cmd.stop = true;
        cmd.debug_info = "No safe path found";
    }
    
    return cmd;
}
bool ObstacleAvoidance::worldToGrid(float x, float y, int& grid_x, int& grid_y) const {
    // 世界坐标: x向右, y向前 (以机器人为中心)
    // 栅格坐标: x向右, y向下 (以图像左上角为原点)
    
    // 将世界坐标转换为栅格坐标
    // x方向: 世界坐标0对应栅格中心
    grid_x = static_cast<int>((x + config_.grid_origin_x) * config_.grid_resolution + config_.grid_width / 2);
    
    // y方向: 世界坐标0对应栅格底部，向前方向对应栅格y减小方向
    grid_y = config_.grid_height - 1 - static_cast<int>((y + config_.grid_origin_y) * config_.grid_resolution);
    
    // // 调试输出
    // std::cout << "World to Grid: x=" << x << ", y=" << y 
    //           << " -> grid_x=" << grid_x << ", grid_y=" << grid_y << std::endl;

    return (grid_x >= 0 && grid_x < config_.grid_width && 
            grid_y >= 0 && grid_y < config_.grid_height);
}



bool ObstacleAvoidance::gridToWorld(int grid_x, int grid_y, float& x, float& y) const {
    if (grid_x < 0 || grid_x >= config_.grid_width || 
        grid_y < 0 || grid_y >= config_.grid_height) {
        return false;
    }
    
    x = static_cast<float>(grid_x) / config_.grid_resolution - config_.grid_origin_x;
    y = static_cast<float>(grid_y) / config_.grid_resolution - config_.grid_origin_y;
    return true;
}

void ObstacleAvoidance::imageToWorld(float u, float v, float depth, float& x, float& y, float& z) const {
    // 检查相机参数有效性
    if (!camera_params_.isValid()) {
        x = y = z = 0;
        return;
    }
    
    // 获取相机内参
    cv::Mat camera_matrix = camera_params_.getCameraMatrix1();
    float fx = camera_matrix.at<float>(0, 0);
    float fy = camera_matrix.at<float>(1, 1);
    float cx = camera_matrix.at<float>(0, 2);
    float cy = camera_matrix.at<float>(1, 2);
    
    // 归一化图像坐标
    float nx = (u - cx) / fx;
    float ny = (v - cy) / fy;
    
    // 考虑相机高度和俯仰角
    z = depth;
    x = nx * z;
    y = ny * z;
    
    // 考虑相机高度
    y += config_.camera_height;
    
    // 考虑相机俯仰角
    if (config_.camera_pitch != 0.0f) {
        float cos_pitch = cos(config_.camera_pitch);
        float sin_pitch = sin(config_.camera_pitch);
        float new_y = y * cos_pitch - z * sin_pitch;
        z = y * sin_pitch + z * cos_pitch;
        y = new_y;
    }
}

cv::Mat ObstacleAvoidance::visualizeCostMap() const {
    cv::Mat display;
    cv::cvtColor(costmap_, display, cv::COLOR_GRAY2BGR);
    
    // 添加网格和坐标轴
    int step = static_cast<int>(50.0f * config_.grid_resolution); // 一个网格
    for (int i = 0; i <= config_.grid_width; i += step) {
        cv::line(display, cv::Point(i, 0), cv::Point(i, config_.grid_height), cv::Scalar(100, 100, 100), 1);
    }
    for (int i = 0; i <= config_.grid_height; i += step) {
        cv::line(display, cv::Point(0, i), cv::Point(config_.grid_width, i), cv::Scalar(100, 100, 100), 1);
    }
    
    // 添加机器人位置（底部中心）
    int robot_x = config_.grid_width / 2;
    int robot_y = config_.grid_height - 1;
    cv::circle(display, cv::Point(robot_x, robot_y), 
               static_cast<int>(config_.robot_radius * config_.grid_resolution), 
               cv::Scalar(0, 255, 0), 2);
    
    // 添加前方方向指示
    cv::arrowedLine(display, 
                   cv::Point(robot_x, robot_y), 
                   cv::Point(robot_x, robot_y - static_cast<int>(config_.robot_radius * 2 * config_.grid_resolution)), 
                   cv::Scalar(0, 255, 0), 2);
    
    return display;
}

cv::Mat ObstacleAvoidance::visualizeDepthMap(const cv::Mat& depth_map) const {
    if (depth_map.empty()) {
        return cv::Mat();
    }
    
    // 归一化深度图以便可视化
    cv::Mat normalized_depth;
    double min_val, max_val;
    cv::minMaxLoc(depth_map, &min_val, &max_val);
    
    if (min_val == max_val) {
        normalized_depth = cv::Mat::zeros(depth_map.size(), CV_8UC1);
    } else {
        depth_map.convertTo(normalized_depth, CV_8UC1, 255.0 / (max_val - min_val), -min_val * 255.0 / (max_val - min_val));
    }
    
    // 应用颜色映射
    cv::Mat color_depth;
    cv::applyColorMap(normalized_depth, color_depth, cv::COLORMAP_JET);
    
    return color_depth;
}

bool ObstacleAvoidance::saveConfig(const std::string& filename) const {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    if (!fs.isOpened()) {
        return false;
    }
    
    fs << "robot_radius" << config_.robot_radius;
    fs << "safety_margin" << config_.safety_margin;
    fs << "max_obstacle_height" << config_.max_obstacle_height;
    fs << "min_obstacle_height" << config_.min_obstacle_height;
    fs << "max_detection_range" << config_.max_detection_range;
    fs << "min_detection_range" << config_.min_detection_range;
    fs << "grid_resolution" << config_.grid_resolution;
    fs << "grid_width" << config_.grid_width;
    fs << "grid_height" << config_.grid_height;
    fs << "grid_origin_x" << config_.grid_origin_x;
    fs << "grid_origin_y" << config_.grid_origin_y;
    fs << "camera_height" << config_.camera_height;
    fs << "camera_pitch" << config_.camera_pitch;
    fs << "max_speed" << config_.max_speed;
    fs << "max_turn_rate" << config_.max_turn_rate;
    
    fs.release();
    return true;
}

bool ObstacleAvoidance::loadConfig(const std::string& filename) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        return false;
    }
    
    fs["robot_radius"] >> config_.robot_radius;
    fs["safety_margin"] >> config_.safety_margin;
    fs["max_obstacle_height"] >> config_.max_obstacle_height;
    fs["min_obstacle_height"] >> config_.min_obstacle_height;
    fs["max_detection_range"] >> config_.max_detection_range;
    fs["min_detection_range"] >> config_.min_detection_range;
    fs["grid_resolution"] >> config_.grid_resolution;
    fs["grid_width"] >> config_.grid_width;
    fs["grid_height"] >> config_.grid_height;
    fs["grid_origin_x"] >> config_.grid_origin_x;
    fs["grid_origin_y"] >> config_.grid_origin_y;
    fs["camera_height"] >> config_.camera_height;
    fs["camera_pitch"] >> config_.camera_pitch;
    fs["max_speed"] >> config_.max_speed;
    fs["max_turn_rate"] >> config_.max_turn_rate;
    
    fs.release();
    return true;
}