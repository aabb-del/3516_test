// debug_logger.cpp
#include "debug_logger.hpp"
#include <sys/stat.h>
#include <iomanip>
#include <chrono>

DebugLogger::DebugLogger(const std::string& base_path) 
    : base_path_(base_path), enabled_(true), log_interval_(30), frame_counter_(0) {
    
    // 创建目录
    mkdir(base_path_.c_str(), 0777);
    
    // 打开文本日志文件
    std::string log_file = base_path_ + "/navigation_log.txt";
    text_log_.open(log_file, std::ios::out | std::ios::app);
    
    if (text_log_.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        text_log_ << "\n=== New Session: " << std::ctime(&in_time_t) << "===\n";
    }
}

DebugLogger::~DebugLogger() {
    if (text_log_.is_open()) {
        text_log_.close();
    }
}

void DebugLogger::setEnabled(bool enabled) {
    enabled_ = enabled;
}

void DebugLogger::setLogInterval(int interval) {
    log_interval_ = interval;
}

bool DebugLogger::logImage(const cv::Mat& image, const std::string& name, int frame_count) {
    if (!enabled_ || frame_count % log_interval_ != 0) {
        return true; // 跳过记录
    }
    
    if (image.empty()) {
        return false;
    }
    
    std::string filename = base_path_ + "/" + name + "_" + 
                          std::to_string(frame_count) + ".png";
    
    return cv::imwrite(filename, image);
}

void DebugLogger::logMessage(const std::string& message) {
    if (!enabled_ || !text_log_.is_open()) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    text_log_ << "[" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") 
              << "] " << message << std::endl;
}

void DebugLogger::logNavigationCommand(const NavigationCommand& cmd) {
    if (!enabled_ || !text_log_.is_open()) {
        return;
    }
    
    std::string status = cmd.stop ? "STOPPED" : "MOVING";
    std::string message = "Navigation: " + status + 
                         ", Linear: " + std::to_string(cmd.linear_velocity) +
                         ", Angular: " + std::to_string(cmd.angular_velocity) +
                         " - " + cmd.debug_info;
    
    logMessage(message);
}