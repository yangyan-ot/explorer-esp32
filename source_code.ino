/**
 * 协议 v1 (传统版本) - 稳定 50Hz 实现
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
 *   - Observer配置文件中请将协议设置为"v2"以便正常识别数据
 * 编写时间：2026年1月4日15:30分
 * 代码使用Qwen3.0 DeepSeek ChatGPT依次优化改进
 */

#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;  // MPU6050 传感器实例

// ================= 配置参数 =================
#define SAMPLE_RATE           50        // 采样频率 50Hz
#define SAMPLE_INTERVAL_US    (1000000UL / SAMPLE_RATE)  // 每次采样间隔时间(微秒)

#define SAMPLES_PER_PACKET    5         // 每个数据包包含的样本数量
#define CALIBRATION_SAMPLES   200       // 校准阶段采集的样本总数

#define TEMP_COEFF_X          0.0f      // X轴温度补偿系数(当前未启用)
#define TEMP_COEFF_Y          0.0f      // Y轴温度补偿系数(当前未启用)
#define TEMP_COEFF_Z          0.0f      // Z轴温度补偿系数(当前未启用)

// ================= 通信协议定义 =================
#define PACKET_HEADER_0       0xFC      // 数据包起始标志字节1
#define PACKET_HEADER_1       0x1B      // 数据包起始标志字节2
#define PACKET_SIZE           66        // 固定数据包大小(字节)

// ================= 全局变量 =================
int32_t offset_x = 0;     // X轴零偏校准值
int32_t offset_y = 0;     // Y轴零偏校准值
int32_t offset_z = 0;     // Z轴零偏校准值

float calib_temp = 0.0f;  // 校准时的传感器温度
float current_temp = 0.0f; // 当前传感器温度

uint32_t lastSampleTimeUs = 0;  // 上次采样时间戳(微秒)

// 采样缓冲区(每个轴独立存储)
int32_t samples_z[SAMPLES_PER_PACKET];  // Z轴样本缓冲区(垂直方向)
int32_t samples_e[SAMPLES_PER_PACKET];  // E轴样本缓冲区(东向)
int32_t samples_n[SAMPLES_PER_PACKET];  // N轴样本缓冲区(北向)
uint8_t sample_idx = 0;               // 当前采样索引位置

// 数据包缓冲区
uint8_t packet_buffer[PACKET_SIZE];   // 预分配66字节的发送缓冲区

// ================= 工具函数 =================
/**
 * 计算校验和
 * @param array 32位整型数组指针
 * @param count 数组元素数量
 * @return 8位异或校验和
 */
uint8_t calculate_checksum(int32_t* array, uint8_t count) {
    uint8_t checksum = 0;
    uint8_t* ptr = (uint8_t*)array;  // 将32位整数数组转为字节数组
    for (uint16_t i = 0; i < count * sizeof(int32_t); i++) {
        checksum ^= ptr[i];  // 逐字节异或运算
    }
    return checksum;
}

// ================= 传感器校准 =================
/**
 * 执行加速度计零偏校准
 * 1. 等待传感器预热
 * 2. 读取参考温度
 * 3. 采集多组样本计算平均零偏
 */
void performCalibration() {
    long sum_x = 0, sum_y = 0, sum_z = 0;  // 各轴原始值累加器
    int16_t ax, ay, az;                    // 临时存储单次读数

    delay(500); // 等待传感器硬件预热稳定

    // 读取校准阶段的参考温度(转换为摄氏度)
    calib_temp = mpu.getTemperature() / 340.0f + 36.53f;

    // 采集指定数量的样本
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        mpu.getAcceleration(&ax, &ay, &az);  // 读取三轴原始加速度值
        sum_x += ax;
        sum_y += ay;
        sum_z += az;
        delay(2);  // 采样间隔
    }

    // 计算各轴零偏平均值
    offset_x = sum_x / CALIBRATION_SAMPLES;
    offset_y = sum_y / CALIBRATION_SAMPLES;
    offset_z = sum_z / CALIBRATION_SAMPLES;
}

// ================= 初始化设置 =================
void setup() {
    Serial.begin(57600);  // 初始化串口通信(调试与数据传输)

    // 初始化I2C总线
    Wire.begin();
    Wire.setClock(400000);  // 设置I2C时钟频率为400kHz(高速模式)

    // 初始化MPU6050传感器
    mpu.initialize();
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);  // 设置量程为±16g

    // 配置数字低通滤波器(匹配50Hz采样率)
    mpu.setDLPFMode(MPU6050_DLPF_BW_20);  // 设置带宽20Hz

    // 执行传感器校准
    performCalibration();

    // 初始化温度及时间变量
    current_temp = calib_temp;
    lastSampleTimeUs = micros();  // 记录启动时间
}

// ================= 主循环 =================
void loop() {
    uint32_t now = micros();  // 获取当前微秒级时间戳

    // 检查是否到达采样时间点(精确时间控制)
    if ((uint32_t)(now - lastSampleTimeUs) >= SAMPLE_INTERVAL_US) {
        lastSampleTimeUs += SAMPLE_INTERVAL_US;  // 更新下次采样时间点

        int16_t ax, ay, az;  // 存储原始传感器读数
        mpu.getAcceleration(&ax, &ay, &az);  // 读取三轴加速度原始值

        // 每50次采样更新一次温度(约1秒)
        static uint8_t temp_counter = 0;
        if (++temp_counter >= 50) {
            // 读取并转换温度值(摄氏度)
            current_temp = mpu.getTemperature() / 340.0f + 36.53f;
            temp_counter = 0;  // 重置计数器
        }

        // 计算当前温度与校准温度的差值
        float temp_diff = current_temp - calib_temp;

        // 应用零偏校准和温度补偿(当前补偿系数为0)
        int32_t val_x = ax - (offset_x + (int32_t)(temp_diff * TEMP_COEFF_X));
        int32_t val_y = ay - (offset_y + (int32_t)(temp_diff * TEMP_COEFF_Y));
        int32_t val_z = az - (offset_z + (int32_t)(temp_diff * TEMP_COEFF_Z));

        // 将校准后的值存入缓冲区(坐标系转换)
        samples_e[sample_idx] = val_x;  // 东向(East)
        samples_n[sample_idx] = val_y;  // 北向(North)
        samples_z[sample_idx] = val_z;  // 垂直向(Zenith)

        sample_idx++;  // 移动到下一个采样位置

        // 检查是否填满一个数据包
        if (sample_idx >= SAMPLES_PER_PACKET) {
            sendPacket();     // 发送完整数据包
            sample_idx = 0;   // 重置采样索引
        }
    }
}

// ================= 数据包发送函数 =================
/**
 * 构建并发送66字节标准数据包
 * 包结构:
 *   [0-1]   : 包头(0xFC 0x1B)
 *   [2-21]  : Z轴5个样本(20字节)
 *   [22-41] : E轴5个样本(20字节)
 *   [42-61] : N轴5个样本(20字节)
 *   [62]    : Z轴校验和
 *   [63]    : E轴校验和
 *   [64]    : N轴校验和
 *   [65]    : 保留字节(0x00)
 */
void sendPacket() {
    // 设置包头标志
    packet_buffer[0] = PACKET_HEADER_0;
    packet_buffer[1] = PACKET_HEADER_1;

    // 填充三轴数据(每轴5个32位整数=20字节)
    memcpy(&packet_buffer[2],  samples_z, 20);  // 垂直轴数据
    memcpy(&packet_buffer[22], samples_e, 20);  // 东向轴数据
    memcpy(&packet_buffer[42], samples_n, 20);  // 北向轴数据

    // 计算并填充各轴校验和
    packet_buffer[62] = calculate_checksum(samples_z, SAMPLES_PER_PACKET);
    packet_buffer[63] = calculate_checksum(samples_e, SAMPLES_PER_PACKET);
    packet_buffer[64] = calculate_checksum(samples_n, SAMPLES_PER_PACKET);

    // 填充保留字节(固定0x00)
    packet_buffer[65] = 0x00;

    // 通过串口发送完整数据包
    Serial.write(packet_buffer, PACKET_SIZE);
}
