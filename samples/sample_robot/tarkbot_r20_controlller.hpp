

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <iomanip>

class TarkbotR20Controller {
public:
    // 构造函数
    TarkbotR20Controller(const std::string& serial_port = "/dev/ttyACM0", 
                         int baud_rate = 230400)
        : serial_fd_(-1), is_running_(false), 
          serial_port_(serial_port), baud_rate_(baud_rate) {
        // 初始化速度为零
        target_velocity_[0] = 0.0f; // X
        target_velocity_[1] = 0.0f; // Y
        target_velocity_[2] = 0.0f; // W
    }

    // 析构函数
    ~TarkbotR20Controller() {
        stop();
        if (serial_fd_ >= 0) {
            close(serial_fd_);
        }
    }

    // 初始化串口连接
    bool initialize() {
        // 打开串口
        serial_fd_ = open(serial_port_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (serial_fd_ < 0) {
            std::cerr << "Error opening serial port: " << serial_port_ << std::endl;
            return false;
        }

        // 配置串口
        struct termios tty;
        if (tcgetattr(serial_fd_, &tty) != 0) {
            std::cerr << "Error getting terminal attributes" << std::endl;
            return false;
        }

        cfsetospeed(&tty, B230400);
        cfsetispeed(&tty, B230400);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8位数据位
        tty.c_iflag &= ~IGNBRK;                         // 禁用BREAK处理
        tty.c_lflag = 0;                                // 无信号字符，无回显
        tty.c_oflag = 0;                                // 无remapping，无延迟
        tty.c_cc[VMIN]  = 0;                            // 读取不需要阻塞
        tty.c_cc[VTIME] = 5;                            // 0.5秒读取超时

        tty.c_iflag &= ~(IXON | IXOFF | IXANY);         // 禁用软件流控
        tty.c_cflag |= (CLOCAL | CREAD);                // 忽略调制解调器控制线
        tty.c_cflag &= ~(PARENB | PARODD);              // 无奇偶校验
        tty.c_cflag &= ~CSTOPB;                         // 1位停止位
        tty.c_cflag &= ~CRTSCTS;                        // 禁用硬件流控

        if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
            std::cerr << "Error setting terminal attributes" << std::endl;
            return false;
        }

        // 启动接收线程
        is_running_ = true;
        receive_thread_ = std::thread(&TarkbotR20Controller::receiveData, this);

        return true;
    }

    // 停止控制器
    void stop() {
        is_running_ = false;
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
        
        // 发送停止命令
        setVelocity(0, 0, 0);
    }

    // 设置速度 (m/s, m/s, rad/s)
    bool setVelocity(float vx, float vy, float vw) {
        std::lock_guard<std::mutex> lock(control_mutex_);
        
        target_velocity_[0] = vx;
        target_velocity_[1] = vy;
        target_velocity_[2] = vw;
        
        // 转换为协议格式 (放大1000倍)
        int16_t vx_int = static_cast<int16_t>(vx * 1000);
        int16_t vy_int = static_cast<int16_t>(vy * 1000);
        int16_t vw_int = static_cast<int16_t>(vw * 1000);
        
        // 构建0x50帧 (速度指令帧)
        std::vector<uint8_t> frame = buildFrame(0x50, {
            static_cast<uint8_t>((vx_int >> 8) & 0xFF),  // Vx高8位
            static_cast<uint8_t>(vx_int & 0xFF),         // Vx低8位
            static_cast<uint8_t>((vy_int >> 8) & 0xFF),  // Vy高8位
            static_cast<uint8_t>(vy_int & 0xFF),         // Vy低8位
            static_cast<uint8_t>((vw_int >> 8) & 0xFF),  // Vw高8位
            static_cast<uint8_t>(vw_int & 0xFF)          // Vw低8位
        });
        
        // 发送帧
        return sendData(frame);
    }

    // 获取最新传感器数据
    struct SensorData {
        float accel[3];      // 加速度 (g)
        float gyro[3];       // 陀螺仪 (°/s)
        float velocity[3];   // 速度 (m/s, m/s, rad/s)
        float battery_voltage; // 电池电压 (V)
    };
    
    SensorData getSensorData() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return sensor_data_;
    }

    // 控制RGB灯光
    bool setRGBLight(uint8_t mode, uint8_t sub_mode, uint8_t time, 
                    uint8_t r, uint8_t g, uint8_t b) {
        // 构建0x52帧 (灯光控制指令)
        std::vector<uint8_t> frame = buildFrame(0x52, {mode, sub_mode, time, r, g, b});
        return sendData(frame);
    }

    // 保存灯光设置到EEPROM
    bool saveLightSettings() {
        // 构建0x53帧 (灯光保存指令)
        std::vector<uint8_t> frame = buildFrame(0x53, {0x55});
        return sendData(frame);
    }

    // 控制蜂鸣器
    bool setBuzzer(bool on) {
        // 构建0x54帧 (蜂鸣器控制指令)
        std::vector<uint8_t> frame = buildFrame(0x54, {static_cast<uint8_t>(on ? 1 : 0)});
        return sendData(frame);
    }

    // 校准IMU
    bool calibrateIMU() {
        // 构建0x51帧 (IMU校准指令)
        std::vector<uint8_t> frame = buildFrame(0x51, {});
        return sendData(frame);
    }

private:
    // 构建协议帧
    std::vector<uint8_t> buildFrame(uint8_t frame_id, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> frame;
        
        // 帧头
        frame.push_back(0xAA);
        frame.push_back(0x55);
        
        // 帧长度 (帧头2 + 长度1 + 帧号1 + 数据N + 校验和1)
        uint8_t length = 5 + data.size();
        frame.push_back(length);
        
        // 帧号
        frame.push_back(frame_id);
        
        // 数据
        frame.insert(frame.end(), data.begin(), data.end());
        
        // 计算校验和
        uint8_t checksum = 0;
        for (uint8_t byte : frame) {
            checksum += byte;
        }
        frame.push_back(checksum);
        
        return frame;
    }

    // 发送数据到串口
    bool sendData(const std::vector<uint8_t>& data) {
        if (serial_fd_ < 0) {
            return false;
        }
        
        // 打印发送的数据用于调试
        std::cout << "Sending: ";
        for (uint8_t byte : data) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl;
        
        ssize_t written = write(serial_fd_, data.data(), data.size());
        return written == static_cast<ssize_t>(data.size());
    }

    // 接收数据线程函数
    // 接收数据线程函数
    void receiveData() {
        ParserState state = STATE_HEAD1;
        uint8_t frame_length = 0;
        uint8_t frame_id = 0;
        uint8_t expected_data_length = 0;
        uint8_t checksum = 0;
        uint8_t data_index = 0;
        std::vector<uint8_t> frame_data;
        
        while (is_running_) {
            uint8_t byte;
            ssize_t n = read(serial_fd_, &byte, 1);
            
            if (n <= 0) {
                // 无数据或错误，继续等待
                usleep(1000); // 休眠1ms
                continue;
            }
            
            // 打印接收的字节用于调试
            std::cout << "Received: " << std::hex << std::setw(2) << std::setfill('0') 
                    << static_cast<int>(byte) << std::dec << std::endl;
            
            switch (state) {
                case STATE_HEAD1:
                    if (byte == 0xAA) {
                        state = STATE_HEAD2;
                        checksum = byte; // 开始计算校验和
                        std::cout << "Found header 1" << std::endl;
                    }
                    break;
                    
                case STATE_HEAD2:
                    if (byte == 0x55) {
                        state = STATE_LENGTH;
                        checksum += byte;
                        std::cout << "Found header 2" << std::endl;
                    } else {
                        state = STATE_HEAD1;
                        std::cout << "Header 2 mismatch, resetting" << std::endl;
                    }
                    break;
                    
                case STATE_LENGTH:
                    frame_length = byte;
                    checksum += byte;
                    
                    // 计算期望的数据长度
                    expected_data_length = frame_length - 5; // 总长度 - 帧头2 - 长度1 - 帧号1 - 校验和1
                    
                    state = STATE_FRAME_ID;
                    std::cout << "Frame length: " << static_cast<int>(frame_length) 
                            << ", expected data: " << static_cast<int>(expected_data_length) << std::endl;
                    break;
                    
                case STATE_FRAME_ID:
                    frame_id = byte;
                    checksum += byte;
                    
                    if (expected_data_length > 0) {
                        frame_data.clear();
                        frame_data.reserve(expected_data_length);
                        data_index = 0;
                        state = STATE_DATA;
                    } else {
                        state = STATE_CHECKSUM;
                    }
                    std::cout << "Frame ID: 0x" << std::hex << static_cast<int>(frame_id) << std::dec << std::endl;
                    break;
                    
                case STATE_DATA:
                    frame_data.push_back(byte);
                    checksum += byte;
                    data_index++;
                    
                    if (data_index >= expected_data_length) {
                        state = STATE_CHECKSUM;
                    }
                    break;
                    
                case STATE_CHECKSUM:
                    {
                        uint8_t calculated_checksum = checksum;
                        if (calculated_checksum == byte) {
                            std::cout << "Checksum OK: 0x" << std::hex << static_cast<int>(calculated_checksum) 
                                    << std::dec << std::endl;
                            // 校验成功，处理帧
                            processFrame(frame_id, frame_data);
                        } else {
                            std::cout << "Checksum error! Expected: 0x" << std::hex 
                                    << static_cast<int>(calculated_checksum) 
                                    << ", Got: 0x" << static_cast<int>(byte) << std::dec << std::endl;
                            // 即使校验和错误，也处理帧（用于调试）
                            processFrame(frame_id, frame_data);
                        }
                        
                        // 重置状态机
                        state = STATE_HEAD1;
                        checksum = 0;
                    }
                    break;
            }
        }
    }

    // 处理接收到的帧
    void processFrame(uint8_t frame_id, const std::vector<uint8_t>& data) {
        switch (frame_id) {
            case 0x10: // 综合数据帧
                if (data.size() >= 20) {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    
                    // 解析加速度数据
                    int16_t accel_x_raw = (data[0] << 8) | data[1];
                    int16_t accel_y_raw = (data[2] << 8) | data[3];
                    int16_t accel_z_raw = (data[4] << 8) | data[5];
                    
                    sensor_data_.accel[0] = (accel_x_raw / 32768.0f) * 2.0f;
                    sensor_data_.accel[1] = (accel_y_raw / 32768.0f) * 2.0f;
                    sensor_data_.accel[2] = (accel_z_raw / 32768.0f) * 2.0f;
                    
                    // 解析陀螺仪数据
                    int16_t gyro_x_raw = (data[6] << 8) | data[7];
                    int16_t gyro_y_raw = (data[8] << 8) | data[9];
                    int16_t gyro_z_raw = (data[10] << 8) | data[11];
                    
                    sensor_data_.gyro[0] = (gyro_x_raw / 32768.0f) * 500.0f;
                    sensor_data_.gyro[1] = (gyro_y_raw / 32768.0f) * 500.0f;
                    sensor_data_.gyro[2] = (gyro_z_raw / 32768.0f) * 500.0f;
                    
                    // 解析速度数据
                    int16_t vel_x_raw = (data[12] << 8) | data[13];
                    int16_t vel_y_raw = (data[14] << 8) | data[15];
                    int16_t vel_w_raw = (data[16] << 8) | data[17];
                    
                    sensor_data_.velocity[0] = vel_x_raw / 1000.0f;
                    sensor_data_.velocity[1] = vel_y_raw / 1000.0f;
                    sensor_data_.velocity[2] = vel_w_raw / 1000.0f;
                    
                    // 解析电池电压
                    uint16_t battery_voltage_raw = (data[18] << 8) | data[19];
                    sensor_data_.battery_voltage = battery_voltage_raw / 100.0f;
                    
                    std::cout << "Sensor data updated" << std::endl;
                } else {
                    std::cout << "Frame 0x10 data too short: " << data.size() 
                              << " bytes (expected 20)" << std::endl;
                }
                break;
                
            default:
                std::cout << "Unknown frame ID: 0x" << std::hex << static_cast<int>(frame_id) 
                          << std::dec << ", data length: " << data.size() << std::endl;
                break;
        }
    }

    // 解析器状态
    enum ParserState {
        STATE_HEAD1,
        STATE_HEAD2,
        STATE_LENGTH,
        STATE_FRAME_ID,
        STATE_DATA,
        STATE_CHECKSUM
    };

    int serial_fd_;                         // 串口文件描述符
    std::atomic<bool> is_running_;          // 运行标志
    std::thread receive_thread_;            // 数据接收线程
    std::mutex control_mutex_;              // 控制互斥锁
    std::mutex data_mutex_;                 // 数据互斥锁
    
    std::string serial_port_;               // 串口设备路径
    int baud_rate_;                         // 波特率
    
    float target_velocity_[3];              // 目标速度 [vx, vy, vw]
    SensorData sensor_data_;                // 传感器数据
};
