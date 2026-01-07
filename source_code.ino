/**
 * 协议 v1 (传统版本)
 * 硬件: ESP32 + MPU6050
 *
 * 采样配置:
 *   - 传感器采样率: 50 Hz
 *   - 每个数据包样本数: 5
 *   - 数据包发送频率: 10 Hz
 *   - 串口波特率：57600
 *
 * 传输协议:
 *   - 固定 66 字节数据包
 *   - 依照AnyShake官方文档Protocol v1 (Legacy)中的协议所上传
 *   - Observer配置文件中请将协议设置为"v1"以便正常识别数据
 * 编写时间：2026年1月7日02:30分
 * 代码使用Qwen3.0 DeepSeek ChatGPT依次优化改进
 */

#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;

// ================= 配置参数 =================
#define SAMPLE_RATE           50        // 传感器原始采样率 (Hz)
#define SAMPLE_INTERVAL_US    (1000000UL / SAMPLE_RATE)  // 采样间隔 (微秒)

#define SAMPLES_PER_PACKET    5         // 每个数据包包含的样本数
#define CALIBRATION_SAMPLES   200       // 校准时采集的样本总数

#define TEMP_COEFF_X          0.0f      // 温度补偿系数 (X轴，当前固定为0)
#define TEMP_COEFF_Y          0.0f      // 温度补偿系数 (Y轴，当前固定为0)
#define TEMP_COEFF_Z          0.0f      // 温度补偿系数 (Z轴，当前固定为0)

#define PACKET_HEADER_0       0xFC      // 数据包起始头字节0
#define PACKET_HEADER_1       0x1B      // 数据包起始头字节1
#define PACKET_SIZE           66        // 完整数据包大小 (字节)

// ================= 全局变量 =================
int32_t offset_x = 0, offset_y = 0, offset_z = 0;  // 三轴加速度计零偏校准值
float calib_temp = 0.0f, current_temp = 0.0f;      // 校准温度与当前温度
uint32_t lastSampleTimeUs = 0;                     // 上次采样时间戳 (微秒)

int32_t samples_z[SAMPLES_PER_PACKET];             // Z轴高通滤波后数据缓冲区
int32_t samples_e[SAMPLES_PER_PACKET];             // E轴 (X轴) 高通滤波后数据缓冲区
int32_t samples_n[SAMPLES_PER_PACKET];             // N轴 (Y轴) 高通滤波后数据缓冲区
uint8_t sample_idx = 0;                            // 当前缓冲区写入索引

uint8_t packet_buffer[PACKET_SIZE];                // 66字节数据包缓冲区

// 高通滤波器状态 (用于消除重力分量)
float filtered_z = 0, filtered_e = 0, filtered_n = 0;
int32_t last_val_z = 0, last_val_e = 0, last_val_n = 0;
const float HPF_ALPHA = 0.995f;                    // 高通滤波器系数 (0.995 = 99.5%保留)

// 动态零偏估计缓冲区（Z轴，用于静止时自动校准）
#define OFFSET_WINDOW 200
int32_t offset_buffer_z[OFFSET_WINDOW];            // Z轴零偏缓冲区 (200个样本)
uint8_t offset_ptr_z = 0;                          // 缓冲区指针

// ================= 工具函数 =================
/**
 * 计算数组校验和 (按字节异或)
 * @param array 需计算校验和的整数数组
 * @param count 数组元素数量
 * @return 校验和字节
 */
uint8_t calculate_checksum(int32_t* array, uint8_t count) {
    uint8_t checksum = 0;
    uint8_t* ptr = (uint8_t*)array;
    for (uint16_t i = 0; i < count * sizeof(int32_t); i++) {
        checksum ^= ptr[i];
    }
    return checksum;
}

/**
 * 更新Z轴动态零偏 (静止时自动校准)
 * @param raw_z 原始Z轴加速度值
 */
void updateZOffset(int32_t raw_z) {
    offset_buffer_z[offset_ptr_z++] = raw_z;
    if (offset_ptr_z >= OFFSET_WINDOW) offset_ptr_z = 0;
    long sum = 0;
    for (int i = 0; i < OFFSET_WINDOW; i++) sum += offset_buffer_z[i];
    offset_z = sum / OFFSET_WINDOW;  // 计算移动平均零偏
}

// ================= 校准 =================
/**
 * 三轴加速度计校准 (静止状态采集平均值)
 * 1. 采集200个样本
 * 2. 计算三轴平均值作为零偏
 * 3. 读取当前温度用于温度补偿 (未实际使用)
 */
void performCalibration() {
    long sum_x = 0, sum_y = 0, sum_z = 0;
    int16_t ax, ay, az;

    delay(500);  // 等待传感器稳定
    calib_temp = mpu.getTemperature() / 340.0f + 36.53f;  // 温度转换公式

    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        mpu.getAcceleration(&ax, &ay, &az);
        sum_x += ax;
        sum_y += ay;
        sum_z += az;
        delay(2);  // 2ms间隔
    }

    offset_x = sum_x / CALIBRATION_SAMPLES;  // X轴零偏
    offset_y = sum_y / CALIBRATION_SAMPLES;  // Y轴零偏
    offset_z = sum_z / CALIBRATION_SAMPLES;  // Z轴零偏
}

// ================= 初始化 =================
void setup() {
    Serial.begin(57600);  // 串口初始化 (57600波特率)
    Wire.begin();         // I2C初始化
    Wire.setClock(400000); // I2C时钟频率 (400kHz)

    mpu.initialize();                     // MPU6050初始化
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);  // 设置±16g量程
    mpu.setDLPFMode(MPU6050_DLPF_BW_20);  // 设置低通滤波带宽20Hz

    performCalibration();  // 执行加速度计校准

    current_temp = calib_temp;  // 初始化当前温度
    lastSampleTimeUs = micros(); // 初始化采样时间戳
}

// ================= 主循环 =================
void loop() {
    uint32_t now = micros();
    // 检查是否到达下一次采样时间
    if ((uint32_t)(now - lastSampleTimeUs) >= SAMPLE_INTERVAL_US) {
        lastSampleTimeUs += SAMPLE_INTERVAL_US;

        int16_t ax, ay, az;
        mpu.getAcceleration(&ax, &ay, &az);  // 读取原始加速度

        // 每50次采样更新一次温度 (约1秒)
        static uint8_t temp_counter = 0;
        if (++temp_counter >= 50) {
            current_temp = mpu.getTemperature() / 340.0f + 36.53f;
            temp_counter = 0;
        }

        float temp_diff = current_temp - calib_temp;  // 温度变化量 (未实际使用)

        // 应用零偏校准
        int32_t val_x = ax - offset_x;
        int32_t val_y = ay - offset_y;
        int32_t val_z = az - offset_z;

        // 高通滤波器 (去除重力分量)
        filtered_e = HPF_ALPHA * (filtered_e + val_x - last_val_e);
        filtered_n = HPF_ALPHA * (filtered_n + val_y - last_val_n);
        filtered_z = HPF_ALPHA * (filtered_z + val_z - last_val_z);
        last_val_e = val_x;  // 更新上一次值
        last_val_n = val_y;
        last_val_z = val_z;

        // 保存滤波后数据到缓冲区
        samples_e[sample_idx] = (int32_t)filtered_e;
        samples_n[sample_idx] = (int32_t)filtered_n;
        samples_z[sample_idx] = (int32_t)filtered_z;

        // 静止状态动态校准Z轴零偏 (当三轴加速度均<1000时)
        if (abs(val_x) < 1000 && abs(val_y) < 1000 && abs(val_z) < 1000) {
            updateZOffset(az);
        }

        sample_idx++;
        if (sample_idx >= SAMPLES_PER_PACKET) {  // 达到5个样本后发送数据包
            sendPacket();
            sample_idx = 0;
        }
    }
}

// ================= 数据包发送 =================
/**
 * 构建并发送66字节数据包
 * 数据结构:
 *   [0-1]  2字节头 (0xFC, 0x1B)
 *   [2-21] Z轴5个样本 (20字节)
 *   [22-41] E轴5个样本 (20字节)
 *   [42-61] N轴5个样本 (20字节)
 *   [62-64] 三轴校验和
 *   [65]   0x00 (填充字节)
 */
void sendPacket() {
    packet_buffer[0] = PACKET_HEADER_0;
    packet_buffer[1] = PACKET_HEADER_1;

    memcpy(&packet_buffer[2],  samples_z, 20);  // 复制Z轴数据 (5×4字节)
    memcpy(&packet_buffer[22], samples_e, 20);  // 复制E轴数据
    memcpy(&packet_buffer[42], samples_n, 20);  // 复制N轴数据

    // 计算并填充校验和
    packet_buffer[62] = calculate_checksum(samples_z, SAMPLES_PER_PACKET);
    packet_buffer[63] = calculate_checksum(samples_e, SAMPLES_PER_PACKET);
    packet_buffer[64] = calculate_checksum(samples_n, SAMPLES_PER_PACKET);
    packet_buffer[65] = 0x00;  // 填充字节

    Serial.write(packet_buffer, PACKET_SIZE);  // 发送完整数据包
}
