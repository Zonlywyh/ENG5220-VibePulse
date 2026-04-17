#ifndef HEARTRATE_DSP_H
#define HEARTRATE_DSP_H

#include <cstdint>

/**
 * @brief Digital Signal Processing class for MAX30102
 * MAX30102 数字信号处理类
 */
class HeartRateDSP
{
private:
    // Filter parameters / 滤波器参数
    float dc_filter_w;    // DC filter state / 直流滤波状态
    float lpf_v_prev;     // Previous low-pass value / 上一次低通滤波值
    const float alpha;    // High-pass coefficient / 高通系数
    const float lpf_beta; // Low-pass coefficient / 低通系数

    // Heart rate tracking / 心率追踪变量
    uint32_t last_peak_time;
    float current_bpm;

    // Peak detection state / 波峰检测状态
    float last_val;
    bool rising;

public:
    /**
     * @brief Constructor: Initialize filter states
     * 构造函数：初始化滤波器状态
     */
    HeartRateDSP();

    /**
     * @brief Unpack 3-byte sensor data to 18-bit integer
     * 将传感器的3字节原始数据解析为18位整数
     *
     */
    uint32_t unpackRawData(uint8_t b1, uint8_t b2, uint8_t b3);

    /**
     * @brief Apply High-pass and Low-pass filters
     * 应用高通和低通滤波器以提取交流信号
     *
     */
    float processSample(float raw_val);

    /**
     * @brief Analyze filtered signal to calculate BPM
     * 分析滤波后的信号并计算每分钟心跳数(BPM)
     */
    void detectHeartRate(float filtered_val, uint32_t timestamp_ms);

    /**
     * @brief Get the latest calculated BPM
     * 获取最新计算的心率值
     */
    float getBPM() const { return current_bpm; }
};

#endif // HEARTRATE_DSP_H
