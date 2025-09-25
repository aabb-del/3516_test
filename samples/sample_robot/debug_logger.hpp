// debug_logger.hpp
#ifndef DEBUG_LOGGER_HPP
#define DEBUG_LOGGER_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <fstream>

#include "obstacle_avoidance.hpp"

class DebugLogger {
public:
    DebugLogger(const std::string& base_path = "/tmp/debug");
    ~DebugLogger();
    
    void setEnabled(bool enabled);
    void setLogInterval(int interval);
    
    bool logImage(const cv::Mat& image, const std::string& name, int frame_count);
    void logMessage(const std::string& message);
    void logNavigationCommand(const NavigationCommand& cmd);
    
private:
    std::string base_path_;
    bool enabled_;
    int log_interval_;
    int frame_counter_;
    std::ofstream text_log_;
};

#endif // DEBUG_LOGGER_HPP