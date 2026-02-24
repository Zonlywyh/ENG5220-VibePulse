#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>

// 基于芯片手册的参数常量
const uint32_t ADC_MASK = 0x3FFFF; // 18位有效位掩码 [cite: 430, 594]
class HeartRateDSP {
private:
    // 滤波器状态变量
    float dc_filter_w = 0;
    float lpf_v_prev = 0;
    const float alpha = 0.95f; // 直流消除系数
    const float lpf_beta = 0.2f; // 简单低通滤波系数

    // 心率计算变量
    uint32_t last_peak_time = 0;
    uint32_t sample_count = 0;
    float current_bpm = 0;

public:
    /**
     * @brief 步骤1：解析有线传来的3字节原始数据
     * 依据手册：18位数据左对齐存储在3个字节中 [cite: 586, 593]
     */
    uint32_t unpackRawData(uint8_t b1, uint8_t b2, uint8_t b3) {
        uint32_t raw = ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
        return raw & ADC_MASK; // 提取18位有效位 [cite: 430]
    }
    /**
     * @brief 步骤2：实时滤波处理
     * 去除直流偏置并平滑波形 
     */
    float processSample(uint32_t raw_val) {
        // A. 直流消除 (High Pass)
        // PPG交流成分非常微弱，直流消除是识别波形的基石 [cite: 427]
        float current_val = (float)raw_val;
        dc_filter_w = current_val + alpha * dc_filter_w;
        float dc_removed = dc_filter_w - lpf_v_prev; // 得到交流分量
        lpf_v_prev = dc_filter_w;

        // B. 简单低通滤波 (消除高频毛刺)
        static float filtered_signal = 0;
        filtered_signal = filtered_signal + lpf_beta * (dc_removed - filtered_signal);
        
        return filtered_signal;
    }

    /**
     * @brief 步骤3：心率识别逻辑 (简化版峰值检测)
     */
    void detectHeartRate(float filtered_val, uint32_t timestamp_ms) {
        static float last_val = 0;
        static bool rising = false;
        const float threshold = 50.0f; // 需根据实际信号振幅调整

        // 简单的斜率检测寻找波峰
        if (filtered_val > threshold && filtered_val < last_val && rising) {
            // 发现波峰
            if (last_peak_time != 0) {
                uint32_t duration = timestamp_ms - last_peak_time;
                if (duration > 300 && duration < 1500) { // 过滤掉不合理的生理间隔
                    current_bpm = 60000.0f / duration;
                    std::cout << "实时心率: " << current_bpm << " BPM" << std::endl;
                    // 此处可调用音乐匹配接口
                }
            }
            last_peak_time = timestamp_ms;
            rising = false;
        } else if (filtered_val > last_val) {
            rising = true;
        }
        last_val = filtered_val;
    }
};
int main() {
    HeartRateDSP dsp;
    // 模拟实时有线传输的数据流 (RED 或 IR 通道) [cite: 584, 811]
    // 实际应用中，你会从串口或网络 Socket 中读取这些字节
    uint8_t mock_bytes[3] = {0x01, 0xF4, 0x00}; // 假设原始数据字节
    // 1. 数据还原
    uint32_t raw = dsp.unpackRawData(mock_bytes[0], mock_bytes[1], mock_bytes[2]);
    // 2. 滤波分析
    float ac_signal = dsp.processSample(raw);
    // 3. 心率识别 (假设采样间隔为10ms，即100Hz采样率) [cite: 739]
    static uint32_t fake_timer = 0;
    dsp.detectHeartRate(ac_signal, fake_timer);
    fake_timer += 10; 
    return 0;
}
