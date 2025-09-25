#ifndef OBSTACLE_AVOIDANCE_HPP
#define OBSTACLE_AVOIDANCE_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "camera_parameters.hpp"  // 包含相机参数类

// 避障配置参数
struct ObstacleAvoidanceConfig {
    float robot_radius = 0.3f;          // 机器人半径（厘米）
    float safety_margin = 0.2f;         // 安全裕量（厘米）
    float max_obstacle_height = 0.5f;   // 最大障碍物高度（厘米）
    float min_obstacle_height = -0.1f;  // 最小障碍物高度（厘米）
    float max_detection_range = 5.0f;   // 最大检测范围（厘米）
    float min_detection_range = 0.3f;   // 最小检测范围（厘米）
    float grid_resolution = 50;           // 栅格地图分辨率（像素/厘米）
    int grid_width = 200;               // 栅格地图宽度（像素）
    int grid_height = 200;              // 栅格地图高度（像素）
    float grid_origin_x = 0.0f;         // 栅格地图原点X（厘米，相对于机器人）
    float grid_origin_y = 0.0f;         // 栅格地图原点Y（厘米，相对于机器人）
    float camera_height = 0.5f;         // 相机高度（厘米）
    float camera_pitch = 0.0f;          // 相机俯仰角（弧度）
    float max_speed = 0.5f;             // 最大速度（厘米/秒）
    float max_turn_rate = 0.5f;         // 最大转向率（弧度/秒）
};

// 导航指令
struct NavigationCommand {
    float linear_velocity = 0.0f;       // 线速度（厘米/秒）
    float angular_velocity = 0.0f;      // 角速度（弧度/秒）
    bool stop = false;                  // 是否停止
    float recommended_direction = 0.0f; // 推荐方向（弧度）
    std::string debug_info;             // 调试信息
};

// 避障处理类
class ObstacleAvoidance {
public:
    // 构造函数，接受相机参数
    ObstacleAvoidance(const CameraParameters& cam_params, 
                     const ObstacleAvoidanceConfig& config = ObstacleAvoidanceConfig());
    ~ObstacleAvoidance();
    
    // 设置/更新配置
    void setConfig(const ObstacleAvoidanceConfig& config);
    ObstacleAvoidanceConfig getConfig() const;
    
    // 设置相机参数
    void setCameraParameters(const CameraParameters& cam_params);
    CameraParameters getCameraParameters() const;
    
    
    // 处理视差图并生成导航指令
    NavigationCommand processDisparityMap(const cv::Mat& disparity, const cv::Mat& Q);

    // 处理深度图并生成导航指令（不再需要单独传递相机参数）
    NavigationCommand processDepthMap(const cv::Mat& depth_map);
    
    // 处理点云并生成导航指令
    NavigationCommand processPointCloud(const cv::Mat& point_cloud);
    
    // 可视化函数
    cv::Mat visualizeCostMap() const;
    cv::Mat visualizeDepthMap(const cv::Mat& depth_map) const;
    cv::Mat visualizeDisparityMap(const cv::Mat& disparity)  const;

    
    // 保存/加载配置
    bool saveConfig(const std::string& filename) const;
    bool loadConfig(const std::string& filename);
    
private:
    // 内部处理函数
    cv::Mat createCostMapFromDepth(const cv::Mat& depth_map);
    cv::Mat createCostMapFromPointCloud(const cv::Mat& point_cloud);
    cv::Mat createCostMapFromDisparity(const cv::Mat& disparity,  const cv::Mat& Q);

    void inflateObstacles(cv::Mat& costmap, int inflation_radius);
    NavigationCommand findSafePath(const cv::Mat& costmap);

    
    // 坐标转换函数
    bool worldToGrid(float x, float y, int& grid_x, int& grid_y) const;
    bool gridToWorld(int grid_x, int grid_y, float& x, float& y) const;
    void imageToWorld(float u, float v, float depth, float& x, float& y, float& z) const;
    void disparityToWorld(float u, float v, float disparity, const cv::Mat& Q, 
                                        float& x, float& y, float& z) const;
    cv::Vec3f transformCameraToRobot(const cv::Vec3f& point_cam) const;
    
    CameraParameters camera_params_;    // 相机参数
    ObstacleAvoidanceConfig config_;    // 避障配置
    cv::Mat costmap_;                   // 当前的代价地图
    cv::Mat last_depth_map_;            // 上一次处理的深度图（用于可视化）
    cv::Mat last_disparity_map_;        // 上一次处理的视差图（用于可视化）
};

#endif // OBSTACLE_AVOIDANCE_HPP