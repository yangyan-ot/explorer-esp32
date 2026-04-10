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
 * 编写时间：2026年04月10日11:00分
 * 代码使用Qwen3.0 DeepSeek ChatGPT依次优化改进
 */
/**
 * 协议 v1 (传统版本)
 * 硬件: ESP32 + MPU6050
 */

#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;

// ===== 配置 =====
#define SAMPLE_RATE           100
#define SAMPLE_INTERVAL_US    (1000000UL / SAMPLE_RATE)

#define SAMPLES_PER_PACKET    5

#define PACKET_HEADER_0       0xFC
#define PACKET_HEADER_1       0x1B
#define PACKET_SIZE           66

// ===== 校准 =====
#define CALIB_SAMPLES 500

int32_t bias_x = 0;
int32_t bias_y = 0;
int32_t bias_z = 0;

// ===== 高通滤波参数 =====
float alpha = 0.995;

// 三轴滤波状态
float hp_x = 0, hp_y = 0, hp_z = 0;
float last_x = 0, last_y = 0, last_z = 0;

// ===== 变量 =====
uint32_t lastSampleTimeUs = 0;

int32_t samples_z[SAMPLES_PER_PACKET];
int32_t samples_e[SAMPLES_PER_PACKET];
int32_t samples_n[SAMPLES_PER_PACKET];

uint8_t sample_idx = 0;

uint8_t packet_buffer[PACKET_SIZE];

// ===== 校验 =====
uint8_t calculate_checksum(int32_t* array, uint8_t count) {
    uint8_t checksum = 0;
    uint8_t* ptr = (uint8_t*)array;

    for (uint16_t i = 0; i < count * sizeof(int32_t); i++) {
        checksum ^= ptr[i];
    }
    return checksum;
}

// ===== 校准 =====
void calibrateMPU() {
    Serial.println("Calibrating... Keep device still!");

    int64_t sum_x = 0, sum_y = 0, sum_z = 0;
    int16_t ax, ay, az;

    for (int i = 0; i < CALIB_SAMPLES; i++) {
        mpu.getAcceleration(&ax, &ay, &az);

        sum_x += ax;
        sum_y += ay;
        sum_z += az;

        delay(5);
    }

    bias_x = sum_x / CALIB_SAMPLES;
    bias_y = sum_y / CALIB_SAMPLES;
    bias_z = sum_z / CALIB_SAMPLES;

    Serial.println("Calibration done!");
}

// ===== 初始化 =====
void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);

    mpu.initialize();
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);

    delay(1000);

    calibrateMPU();

    lastSampleTimeUs = micros();
}

// ===== 主循环 =====
void loop() {
    uint32_t now = micros();

    if ((uint32_t)(now - lastSampleTimeUs) >= SAMPLE_INTERVAL_US) {
        lastSampleTimeUs += SAMPLE_INTERVAL_US;

        int16_t ax, ay, az;
        mpu.getAcceleration(&ax, &ay, &az);

        // ===== 去bias =====
        float x = ax - bias_x;
        float y = ay - bias_y;
        float z = az - bias_z;

        // ===== 高通滤波（自动回零）=====
        hp_x = alpha * (hp_x + x - last_x);
        hp_y = alpha * (hp_y + y - last_y);
        hp_z = alpha * (hp_z + z - last_z);

        last_x = x;
        last_y = y;
        last_z = z;

        // ===== 存储 =====
        samples_e[sample_idx] = (int32_t)hp_x;
        samples_n[sample_idx] = (int32_t)hp_y;
        samples_z[sample_idx] = (int32_t)hp_z;

        sample_idx++;

        if (sample_idx >= SAMPLES_PER_PACKET) {
            sendPacket();
            sample_idx = 0;
        }
    }
}

// ===== 发送 =====
void sendPacket() {
    packet_buffer[0] = PACKET_HEADER_0;
    packet_buffer[1] = PACKET_HEADER_1;

    memcpy(&packet_buffer[2],  samples_z, 20);
    memcpy(&packet_buffer[22], samples_e, 20);
    memcpy(&packet_buffer[42], samples_n, 20);

    packet_buffer[62] = calculate_checksum(samples_z, SAMPLES_PER_PACKET);
    packet_buffer[63] = calculate_checksum(samples_e, SAMPLES_PER_PACKET);
    packet_buffer[64] = calculate_checksum(samples_n, SAMPLES_PER_PACKET);
    packet_buffer[65] = 0x00;

    Serial.write(packet_buffer, PACKET_SIZE);
}
