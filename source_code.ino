/**
 * 协议 v1 (传统版本)
 * 硬件: ESP32 + MPU6050
 *
 * 采样配置:
 *   - 传感器采样率: 100 Hz
 *   - 每个数据包样本数: 5
 *   - 数据包发送频率: 20 Hz
 *   - 串口波特率：115200
 *
 * 传输协议:
 *   - 固定 66 字节数据包
 *   - 依照AnyShake官方文档Protocol v1 (Legacy)中的协议所上传
 *   - Observer配置文件中请将协议设置为"v1"以便正常识别数据
 * 编写时间：2026年04月01日23:10分
 * 代码使用Qwen3.0 DeepSeek ChatGPT依次优化改进
 */
/**
 * 协议 v1 (传统版本)
 * 硬件: ESP32 + MPU6050
 */

#include <Wire.h>        // I2C通信库
#include <MPU6050.h>     // MPU6050传感器库

MPU6050 mpu;             // 创建MPU6050对象

// ===== 配置 =====
#define SAMPLE_RATE           100                         // 采样频率（Hz）
#define SAMPLE_INTERVAL_US    (1000000UL / SAMPLE_RATE)   // 采样间隔（微秒）

#define SAMPLES_PER_PACKET    5   // 每个数据包包含5组数据

#define PACKET_HEADER_0       0xFC   // 数据包头字节1
#define PACKET_HEADER_1       0x1B   // 数据包头字节2
#define PACKET_SIZE           66     // 整个数据包长度（字节）

// ===== 校准配置 =====
#define CALIB_SAMPLES 500   // 校准采样次数（约5秒）

int32_t bias_x = 0;   // X轴零偏
int32_t bias_y = 0;   // Y轴零偏
int32_t bias_z = 0;   // Z轴零偏

// ===== 变量 =====
uint32_t lastSampleTimeUs = 0;   // 上一次采样时间（微秒）

// 存储一包数据（5个样本）
int32_t samples_z[SAMPLES_PER_PACKET];
int32_t samples_e[SAMPLES_PER_PACKET];  // east（x轴）
int32_t samples_n[SAMPLES_PER_PACKET];  // north（y轴）
uint8_t sample_idx = 0;                 // 当前采样索引

uint8_t packet_buffer[PACKET_SIZE];     // 发送缓冲区

// ===== 校验函数 =====
// 对数据做异或校验（简单校验）
uint8_t calculate_checksum(int32_t* array, uint8_t count) {
    uint8_t checksum = 0;
    uint8_t* ptr = (uint8_t*)array;   // 转为字节指针

    // 遍历所有字节做 XOR
    for (uint16_t i = 0; i < count * sizeof(int32_t); i++) {
        checksum ^= ptr[i];
    }
    return checksum;
}

// ===== 校准函数 =====
// 开机时测量静止状态下的偏移量（零偏）
void calibrateMPU() {
    Serial.println("Calibrating... Keep device still!");

    int64_t sum_x = 0;
    int64_t sum_y = 0;
    int64_t sum_z = 0;

    int16_t ax, ay, az;

    // 多次采样取平均
    for (int i = 0; i < CALIB_SAMPLES; i++) {
        mpu.getAcceleration(&ax, &ay, &az);

        sum_x += ax;
        sum_y += ay;
        sum_z += az;

        delay(5); // 稍微延时，让数据更稳定
    }

    // 计算平均值作为偏移量
    bias_x = sum_x / CALIB_SAMPLES;
    bias_y = sum_y / CALIB_SAMPLES;
    bias_z = sum_z / CALIB_SAMPLES;

    Serial.println("Calibration done!");
    Serial.print("Bias X: "); Serial.println(bias_x);
    Serial.print("Bias Y: "); Serial.println(bias_y);
    Serial.print("Bias Z: "); Serial.println(bias_z);
}

// ===== 初始化 =====
void setup() {
    Serial.begin(115200);   // 初始化串口
    Wire.begin();           // 初始化I2C
    Wire.setClock(400000);  // 设置I2C为400kHz高速模式

    mpu.initialize();       // 初始化MPU6050

    // 设置加速度量程（±2g，精度最高）
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);

    delay(1000); // 等待传感器稳定

    calibrateMPU();  // 开机自动校准

    lastSampleTimeUs = micros(); // 记录当前时间
}

// ===== 主循环 =====
void loop() {
    uint32_t now = micros(); // 当前时间（微秒）

    // 到达采样时间
    if ((uint32_t)(now - lastSampleTimeUs) >= SAMPLE_INTERVAL_US) {
        lastSampleTimeUs += SAMPLE_INTERVAL_US;

        int16_t ax, ay, az;
        mpu.getAcceleration(&ax, &ay, &az); // 读取加速度

        // 减去零偏（让静止时输出接近0）
        samples_e[sample_idx] = ax - bias_x;
        samples_n[sample_idx] = ay - bias_y;
        samples_z[sample_idx] = az - bias_z;

        sample_idx++;

        // 收集满一包数据
        if (sample_idx >= SAMPLES_PER_PACKET) {
            sendPacket();      // 发送数据
            sample_idx = 0;    // 重置索引
        }
    }
}

// ===== 数据发送 =====
void sendPacket() {
    // 写入包头
    packet_buffer[0] = PACKET_HEADER_0;
    packet_buffer[1] = PACKET_HEADER_1;

    // 拷贝数据（每组5个int32 = 20字节）
    memcpy(&packet_buffer[2],  samples_z, 20);
    memcpy(&packet_buffer[22], samples_e, 20);
    memcpy(&packet_buffer[42], samples_n, 20);

    // 添加校验
    packet_buffer[62] = calculate_checksum(samples_z, SAMPLES_PER_PACKET);
    packet_buffer[63] = calculate_checksum(samples_e, SAMPLES_PER_PACKET);
    packet_buffer[64] = calculate_checksum(samples_n, SAMPLES_PER_PACKET);
    packet_buffer[65] = 0x00;  // 预留字节

    // 发送整个数据包
    Serial.write(packet_buffer, PACKET_SIZE);
}
