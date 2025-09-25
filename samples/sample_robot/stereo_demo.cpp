
#include "camera_parameters.hpp"
#include "stereo_match.hpp"
#include "obstacle_avoidance.hpp"
#include "debug_logger.hpp"
#include "video_frame_vb.hpp"
#include "raii_wrappers.hpp"
#include "image_conversion.h"



#include <thread>
#include <atomic>
#include <chrono>

extern std::atomic<bool> g_should_exit;

int stereo_matcher_test(){
    
    
    // 创建立体匹配器
    StereoMatcher::Parameters params;
    params.algorithm = StereoMatcher::STEREO_BM;
    params.maxDisparity = 256;
    params.blockSize = 15;
    params.scale = 0.25f; // 缩小图像以加快处理速度
    

        // 加载相机参数
    CameraParameters cam_params;
    if (!cam_params.loadFromFiles("intrinsics.yml", "extrinsics.yml")) {
        std::cerr << "Failed to load camera parameters" << std::endl;
        return -1;
    }
    
    StereoMatcher matcher(params, cam_params);
    
    cv::Mat left = cv::imread("left_20cm.jpg", cv::IMREAD_GRAYSCALE);
    cv::Mat right = cv::imread("right_20cm.jpg", cv::IMREAD_GRAYSCALE);
    cv::Mat disparity;


    std::string algo;
    if(StereoMatcher::STEREO_BM == params.algorithm)
    {
        algo = "bm";
    }
    else
    {
        algo = "todo";
    }


    std::string disparity_name = "disparity_" + algo + 
                                "_disparity_" + std::to_string(params.maxDisparity) + 
                                "_blocksize_" + std::to_string(params.blockSize) + 
                                "_scale_" + std::to_string(params.scale) +
                                ".png";

    std::string disparity_color_name = "disparity_color_" + algo + 
                                "_disparity_" + std::to_string(params.maxDisparity) + 
                                "_blocksize_" + std::to_string(params.blockSize) + 
                                "_scale_" + std::to_string(params.scale) +
                                ".png";

    std::string pointcloud_name = "pointcloud_" + algo + 
                                "_disparity_" + std::to_string(params.maxDisparity) + 
                                "_blocksize_" + std::to_string(params.blockSize) + 
                                "_scale_" + std::to_string(params.scale) +
                                ".txt";


    if (matcher.computeDisparity(left, right, disparity)) {

        cv::Mat disp8_1c;
        matcher.toDisplay(disparity, disp8_1c);
        cv::imwrite(disparity_name, disp8_1c);

        // 进行颜色映射
        cv::Mat disp8_3c;
        cv::applyColorMap(disp8_1c, disp8_3c, cv::COLORMAP_TURBO);
        cv::imwrite(disparity_color_name, disp8_3c);
    
    
        cv::Mat pointCloud;
        if (matcher.computePointCloud(disparity, pointCloud)) {
            matcher.savePointCloud(pointcloud_name, pointCloud);
        }
    }
    
    return 0;
}





int stereo_matcher_test_hi() {

    
    // 加载相机参数
    CameraParameters cam_params;
    if (!cam_params.loadFromFiles("intrinsics.yml", "extrinsics.yml")) {
        std::cerr << "Failed to load camera parameters" << std::endl;
        return -1;
    }

    // 创建立体匹配器
    StereoMatcher::Parameters params;
    params.algorithm = StereoMatcher::STEREO_BM;
    params.maxDisparity = 256;
    params.blockSize = 15;
    params.scale = 0.5f; // 缩小图像以加快处理速度

    StereoMatcher matcher(params, cam_params);


    // 创建避障处理器
    // 配置避障参数
    ObstacleAvoidanceConfig oa_config;
    oa_config.robot_radius = 0.1f * 100;
    oa_config.safety_margin = 0.1f * 100;
    oa_config.max_obstacle_height = 1.0f * 100;  // 最大高度，单位cm    // 高度范围
    oa_config.min_obstacle_height = -0.2f * 100; // 最小高度
    oa_config.max_detection_range = 2.0f * 100; // 增加检测范围         // 机器人前后范围
    oa_config.min_detection_range = -2.0f * 100;  // 减少最小检测范围
    oa_config.grid_resolution = 2.0f;
    oa_config.grid_width = 200;
    oa_config.grid_height = 200;
    oa_config.camera_height = 0.2f * 100;

    // 创建避障处理器
    ObstacleAvoidance obstacle_avoidance(cam_params, oa_config);


    DebugLogger debug_logger("/mnt/TF/debug_logs");
    debug_logger.setLogInterval(30); // 每30帧记录一次

    int frame_count = 0;

    while(!g_should_exit.load())
    {
        VIFrameRAII left_frame(0, 0);
        VIFrameRAII right_frame(1, 0);
        
        if (!left_frame.acquire() || !right_frame.acquire()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // 转换为OpenCV Mat
        cv::Mat left, right;
        if (!ImageConversion::yuv_to_gray(*(left_frame.get()), left) || 
            !ImageConversion::yuv_to_gray(*(right_frame.get()), right)) {
            std::cout << "Failed to convert YUV to BGR" << std::endl;
            continue;
        }


        cv::Mat disparity;
        cv::Mat disp8_3c;
        cv::Mat disp8_1c;
        cv::Mat pointCloud;

        if (matcher.computeDisparity(left, right, disparity)) {
            
            matcher.toDisplay(disparity, disp8_1c);
            cv::imwrite("disparity.png", disp8_1c);
            
            // 进行颜色映射
            cv::applyColorMap(disp8_1c, disp8_3c, cv::COLORMAP_TURBO);
            cv::imwrite("disparity_color.png", disp8_3c);
        
            // 计算点云和保存
            if (matcher.computePointCloud(disparity, pointCloud)) {
                matcher.savePointCloud("pointcloud_xyz.txt", pointCloud);
            }
        }
        else
        {
            continue;
        }

        

        // 处理视差图
        // NavigationCommand cmd = obstacle_avoidance.processDisparityMap(disparity);

        // 点云
        NavigationCommand cmd = obstacle_avoidance.processPointCloud(pointCloud);

        
        
        // 输出导航指令
        std::cout << "Navigation Command: " << cmd.debug_info << std::endl;
        if (!cmd.stop) {
            std::cout << "Linear: " << cmd.linear_velocity 
                    << ", Angular: " << cmd.angular_velocity << std::endl;
        } else {
            std::cout << "STOPPING" << std::endl;
        }

            
        // 代价地图可视化，彩色图像，显示机器人前方的障碍物分布和安全性评估
        cv::Mat costmap_viz = obstacle_avoidance.visualizeCostMap();
        
        // 深度图可视化，彩色化的深度信息图像
        cv::Mat disparity_viz = obstacle_avoidance.visualizeDisparityMap(disparity);
            
        debug_logger.logImage(costmap_viz, "costmap", frame_count);
        debug_logger.logImage(disparity_viz, "disparity", frame_count);
        
        // 输出到控制台（可选）
        std::cout << "Frame: " << frame_count << " - " << cmd.debug_info << std::endl;
            
        frame_count++;

        // 代价图显示
        VideoFrameVB videoFrame(1920, 1080);
        videoFrame.loadFromMat(costmap_viz, false);
        videoFrame.flushCache();
        HI_MPI_VO_SendFrame(0, 2, videoFrame.getFrameInfo(), -1);

        // 视差图显示
        VideoFrameVB videoFrameColor(1920, 1080);
        videoFrameColor.loadFromMat(disp8_3c, false);
        videoFrameColor.flushCache();
        HI_MPI_VO_SendFrame(0, 3, videoFrameColor.getFrameInfo(), -1);


        // 送到显示屏显示
        HI_MPI_VO_SendFrame(0, 0, left_frame.get(), -1);
        HI_MPI_VO_SendFrame(0, 1, right_frame.get(), -1);

    }



    return 0;
}
