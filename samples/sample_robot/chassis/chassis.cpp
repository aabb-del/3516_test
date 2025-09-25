#include "chassis.h"


#include "serial.h"





#include <stdio.h>
#include <stdint.h>
#include <string.h>

// 定义状态机状态
typedef enum {
    STATE_HEAD1,
    STATE_HEAD2,
    STATE_LENGTH,
    STATE_FRAME_ID,
    STATE_DATA,
    STATE_CHECKSUM
} parser_state_t;

// 解析器结构体
typedef struct {
    parser_state_t state;
    uint8_t buffer[256]; // 足够大的缓冲区存储一帧数据
    uint8_t data_index;
    uint8_t frame_length;
    uint8_t checksum;
    uint8_t frame_id;
    uint8_t expected_data_length;
} parser_t;

// 初始化解析器
void parser_init(parser_t *parser) {
    parser->state = STATE_HEAD1;
    parser->data_index = 0;
    parser->frame_length = 0;
    parser->checksum = 0;
    parser->frame_id = 0;
    parser->expected_data_length = 0;
    memset(parser->buffer, 0, sizeof(parser->buffer));
}

// 处理接收到的字节
int parser_process_byte(parser_t *parser, uint8_t byte) {
    switch (parser->state) {
        case STATE_HEAD1:
            if (byte == 0xAA) {
                parser->state = STATE_HEAD2;
                parser->checksum = byte; // 开始计算校验和
            }
            break;

        case STATE_HEAD2:
            if (byte == 0x55) {
                parser->state = STATE_LENGTH;
                parser->checksum += byte;
            } else {
                parser_init(parser); // 重置状态机
            }
            break;

        case STATE_LENGTH:
            parser->frame_length = byte;
            parser->checksum += byte;
            
            // 计算期望的数据长度: 总长度 - 帧头(2) - 长度(1) - 帧号(1) - 校验和(1)
            parser->expected_data_length = parser->frame_length - 5;
            
            parser->state = STATE_FRAME_ID;
            break;

        case STATE_FRAME_ID:
            parser->frame_id = byte;
            parser->checksum += byte;
            
            if (parser->expected_data_length > 0) {
                parser->state = STATE_DATA;
            } else {
                parser->state = STATE_CHECKSUM;
            }
            break;

        case STATE_DATA:
            parser->buffer[parser->data_index++] = byte;
            parser->checksum += byte;
            
            // 检查是否已读取所有数据
            if (parser->data_index >= parser->expected_data_length) {
                parser->state = STATE_CHECKSUM;
            }
            break;

        case STATE_CHECKSUM:
            // 验证校验和
            uint8_t calculated_checksum = parser->checksum & 0xFF;
            if (calculated_checksum == byte) {
                // 校验成功，返回帧ID表示成功解析一帧
                int result = parser->frame_id;
                // 不重置解析器，让外部代码处理数据后再重置
                return result;
            } else {
                // 校验失败
                printf("Checksum error! Expected: 0x%02X, Got: 0x%02X\n", 
                       calculated_checksum, byte);
                parser_init(parser);
                return -1;
            }
            break;

        default:
            parser_init(parser);
            break;
    }
    return 0; // 尚未解析到完整帧
}

// 解析0x10帧数据（综合数据帧）
void parse_frame_0x10(uint8_t *data, uint8_t length) {
    if (length < 20) {
        printf("Frame 0x10 data too short: %d bytes (expected 20)\n", length);
        printf("Data: ");
        for (int i = 0; i < length; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return;
    }
    
    // 解析加速度数据 (6字节: X高8位, X低8位, Y高8位, Y低8位, Z高8位, Z低8位)
    int16_t accel_x = (data[0] << 8) | data[1];
    int16_t accel_y = (data[2] << 8) | data[3];
    int16_t accel_z = (data[4] << 8) | data[5];
    
    // 解析陀螺仪数据 (6字节)
    int16_t gyro_x = (data[6] << 8) | data[7];
    int16_t gyro_y = (data[8] << 8) | data[9];
    int16_t gyro_z = (data[10] << 8) | data[11];
    
    // 解析速度数据 (6字节)
    int16_t vel_x = (data[12] << 8) | data[13];
    int16_t vel_y = (data[14] << 8) | data[15];
    int16_t vel_w = (data[16] << 8) | data[17];
    
    // 解析电池电压 (2字节)
    uint16_t battery_voltage = (data[18] << 8) | data[19];
    float battery_voltage_f = battery_voltage / 100.0f;
    
    // 转换为实际物理值
    float accel_x_f = (accel_x / 32768.0f) * 2.0f;
    float accel_y_f = (accel_y / 32768.0f) * 2.0f;
    float accel_z_f = (accel_z / 32768.0f) * 2.0f;
    
    float gyro_x_f = (gyro_x / 32768.0f) * 500.0f;
    float gyro_y_f = (gyro_y / 32768.0f) * 500.0f;
    float gyro_z_f = (gyro_z / 32768.0f) * 500.0f;
    
    float vel_x_f = vel_x / 1000.0f;
    float vel_y_f = vel_y / 1000.0f;
    float vel_w_f = vel_w / 1000.0f;
    
    printf("Frame 0x10:\n");
    printf("  Accel: X=%.3fg, Y=%.3fg, Z=%.3fg\n", accel_x_f, accel_y_f, accel_z_f);
    printf("  Gyro:  X=%.3f°/s, Y=%.3f°/s, Z=%.3f°/s\n", gyro_x_f, gyro_y_f, gyro_z_f);
    printf("  Vel:   X=%.3fm/s, Y=%.3fm/s, W=%.3frad/s\n", vel_x_f, vel_y_f, vel_w_f);
    printf("  Battery: %.2fV\n", battery_voltage_f);
}

// 处理完整帧数据
void handle_complete_frame(parser_t *parser) {
    switch (parser->frame_id) {
        case 0x10: // 综合数据帧
            parse_frame_0x10(parser->buffer, parser->data_index);
            break;
        default:
            printf("Unknown frame ID: 0x%02X, Data length: %d\n", parser->frame_id, parser->data_index);
            // 打印原始数据用于调试
            printf("Raw data: ");
            for (int i = 0; i < parser->data_index; i++) {
                printf("%02X ", parser->buffer[i]);
            }
            printf("\n");
            break;
    }
}

// 处理串口数据
void process_serial_data(uint8_t *data, size_t length) {
    static parser_t parser;
    static int initialized = 0;
    
    if (!initialized) {
        parser_init(&parser);
        initialized = 1;
    }
    
    for (size_t i = 0; i < length; i++) {
        int result = parser_process_byte(&parser, data[i]);
        
        if (result > 0) {
            printf("Successfully parsed frame 0x%02X\n", result);
            handle_complete_frame(&parser);
            parser_init(&parser); // 重置解析器处理下一帧
        } else if (result < 0) {
            printf("Checksum error!\n");
            // 解析器已经在parser_process_byte中重置
        }
    }
}

int main_test() {
    // 您提供的真实数据
    uint8_t real_data[] = {
        0xaa, 0x55, 0x19, 0x10, 0x0, 0x62, 0xff, 0x20, 0x40, 0xce, 
        0xff, 0xfb, 0x0, 0xc, 0xff, 0xfa, 0x0, 0x0, 0x0, 0x0, 
        0x0, 0x0, 0x4, 0xb2, 0x6c
    };
    
    printf("Processing real data...\n");
    printf("Data length: %ld bytes\n", sizeof(real_data));
    printf("Data: ");
    for (int i = 0; i < sizeof(real_data); i++) {
        printf("%02X ", real_data[i]);
    }
    printf("\n\n");
    
    // 手动验证校验和
    uint8_t checksum = 0;
    for (int i = 0; i < sizeof(real_data) - 1; i++) {
        checksum += real_data[i];
    }
    printf("Manual checksum calculation: 0x%02X (expected 0x%02X)\n", 
           checksum & 0xFF, real_data[sizeof(real_data) - 1]);
    
    // 处理数据
    process_serial_data(real_data, sizeof(real_data));
    
    return 0;
}






void Chassis::test()
{
    cout << "111111111111" << endl;

    serial_t *serial;
    const char* device = "/dev/ttyCH343USB0";
    uint8_t buffer[1024];

    serial = serial_new();
    if(serial_open(serial, device, 230400) < 0)
    {
        cout << "open device failed" << endl;
        perror("serial_open");
    }

    main_test();

    getchar();

    while(1)
    {
        int size = serial_read(serial, buffer, 1024, 10);
        if(size > 0)
        {

            process_serial_data(buffer, size);

            for(int i=0; i<size; i++)
            {
                printf("0x%0x", buffer[i]);
                if(i != size-1) printf(" ");
                else printf("\n");
            }
        }
    }

}