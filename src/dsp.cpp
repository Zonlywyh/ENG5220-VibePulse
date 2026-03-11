#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>

// Constants based on chip datasheet / 基于芯片手册的参数常量
const uint32_t ADC_MASK = 0x3FFFF; // 18-bit effective bit mask / 18位有效位掩码

class HeartRateDSP {
private:
    // Filter state variables / 滤波器状态变量
    float dc_filter_w = 0;
    float lpf_v_prev = 0;
    const float alpha = 0.95f;    // DC removal coefficient / 直流消除系数
    const float lpf_beta = 0.2f;   // Low-pass filter coefficient / 低通滤波系数

    // Heart rate calculation / 心率计算变量
    uint32_t last_peak_time = 0;
    float current_bpm = 0;

public:
    /**
     * @brief Step 1: Unpack 3-byte raw data from wire
     * 步骤1：解析有线传来的3字节原始数据
     * Per manual: 18-bit data stored in 3 bytes
     * 依据手册：18位数据存储在3个字节中
     */
    uint32_t unpackRawData(uint8_t b1, uint8_t b2, uint8_t b3) {
        // Combine bytes into a 24-bit integer / 将字节合并为24位整数
        uint32_t raw = ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
        // Shift or mask based on alignment (Assumes right-aligned 18-bit)
        // 根据对齐方式提取（假设为右对齐的18位数据）
        return raw & ADC_MASK; 
    }

    /**
     * @brief Step 2: Real-time filtering
     * 步骤2：实时滤波处理
     * Removes DC offset and smoothes waveform / 去除直流偏置并平滑波形
     */
    float processSample(uint32_t raw_val) {
        // A. DC Removal (High Pass) / 直流消除 (高通滤波)
        // PPG AC component is weak; DC removal is the cornerstone of wave recognition
        // PPG交流成分非常微弱，直流消除是识别波形的基石
        float current_val = (float)raw_val;
        
        // Basic IIR High-pass filter / 基础IIR高通滤波器
        float prev_w = dc_filter_w;
        dc_filter_w = current_val + alpha * prev_w;
        float dc_removed = dc_filter_w - prev_w; 

        // B. Low-pass Filter (Noise reduction) / 低通滤波 (消除噪声)
        static float filtered_signal = 0;
        filtered_signal = filtered_signal + lpf_beta * (dc_removed - filtered_signal);

        return filtered_signal;
    }

    /**
     * @brief Step 3: Heart rate detection logic (Peak Detection)
     * 步骤3：心率识别逻辑 (波峰检测)
     */
    void detectHeartRate(float filtered_val, uint32_t timestamp_ms) {
        static float last_val = 0;
        static bool rising = false;
        // Threshold should be dynamic in production / 生产环境中阈值应为动态
        const float threshold = 50.0f; 

        // Simple slope detection to find peaks / 简单的斜率检测寻找波峰
        if (filtered_val > threshold && filtered_val < last_val && rising) {
            // Peak discovered / 发现波峰
            if (last_peak_time != 0) {
                uint32_t duration = timestamp_ms - last_peak_time;
                // Filter unrealistic physiological intervals / 过滤掉不合理的生理间隔
                if (duration > 300 && duration < 1500) { 
                    current_bpm = 60000.0f / duration;
                    std::cout << "Real-time HR: " << current_bpm << " BPM" << std::endl;
                    std::cout << "实时心率: " << current_bpm << " BPM" << std::endl;
                }
            }
            last_peak_time = timestamp_ms;
            rising = false;
        }
        else if (filtered_val > last_val) {
            rising = true;
        }
        last_val = filtered_val;
    }
};

int main() {
    HeartRateDSP dsp;

    // Simulated data stream (RED or IR channel) / 模拟实时数据流 (红光或红外通道)
    uint8_t mock_bytes[3] = { 0x01, 0xF4, 0x00 }; 

    // 1. Data Recovery / 数据还原
    uint32_t raw = dsp.unpackRawData(mock_bytes[0], mock_bytes[1], mock_bytes[2]);

    // 2. Filter Analysis / 滤波分析
    float ac_signal = dsp.processSample(raw);

    // 3. HR Recognition (Assume 100Hz sampling rate) / 心率识别 (假设100Hz采样率)
    static uint32_t fake_timer = 0;
    dsp.detectHeartRate(ac_signal, fake_timer);
    fake_timer += 10; // 10ms interval / 10毫秒间隔

    return 0;
}
