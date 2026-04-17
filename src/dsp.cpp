#include "../include/dsp.h"
#include <iostream>
#include <cmath>

// Step 1: Unpack 3-byte raw data / 步骤1：解析3字节原始数据
uint32_t HeartRateDSP::unpackRawData(uint8_t b1, uint8_t b2, uint8_t b3)
{
    // Combine bytes into 18-bit integer / 将字节组合成18位整数
    uint32_t raw = ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
    return raw & 0x3FFFF; // Extract valid 18 bits / 提取18位有效位
}

// Step 2: Real-time filtering / 步骤2：实时滤波处理
float HeartRateDSP::processSample(float raw_val)
{
    float current_val = raw_val;

    // A. DC Removal (High Pass Filter) / A. 直流消除（高通滤波）
    // Formula: w(n) = x(n) + alpha * w(n-1); y(n) = w(n) - w(n-1)
    float prev_w = dc_filter_w;
    dc_filter_w = current_val + alpha * prev_w;
    float dc_removed = dc_filter_w - prev_w;

    // B. Low Pass Filter (LPF) / B. 低通滤波 (消除高频噪声)
    static float filtered_signal = 0.0f;
    filtered_signal = filtered_signal + lpf_beta * (dc_removed - filtered_signal);

    return filtered_signal;
}

// Step 3: Heart Rate Detection Logic / 步骤3：心率识别逻辑
void HeartRateDSP::detectHeartRate(float filtered_val, uint32_t timestamp_ms)
{
    static float last_val = 0.0f;
    static bool rising = false;

    // Sensitivity threshold / 灵敏度阈值
    const float threshold = 0.0001f;

    // 1. Peak Detection: Value drops after rising above threshold
    // 1. 波峰判断：数值在超过阈值后开始下降
    if (rising && filtered_val < last_val && last_val > threshold)
    {
        if (last_peak_time != 0)
        {
            uint32_t duration = timestamp_ms - last_peak_time;

            // 2. Physiological Filter: 250ms(240BPM) to 1500ms(40BPM)
            // 2. 生理性过滤：过滤掉不合理的心跳频率
            if (duration >= 250 && duration <= 1500)
            {
                float raw_bpm = 60000.0f / static_cast<float>(duration);

                // 3. Dynamic Smoothing (EMA) / 3. 动态平滑处理 (使数值变动更稳)
                if (current_bpm < 10.0f)
                {
                    current_bpm = raw_bpm; // Initial assignment / 初始赋值
                }
                else
                {
                    current_bpm = (current_bpm * 0.8f) + (raw_bpm * 0.2f);
                }

                std::cout << "[DSP] Peak Detected! BPM: " << (int)(current_bpm + 0.5) << std::endl;
            }
        }
        last_peak_time = timestamp_ms;
        rising = false; // Start descending / 开始进入下降沿
    }
    // 2. Detect rising edge / 2. 检测上升沿
    else if (filtered_val > last_val)
    {
        rising = true;
    }

    last_val = filtered_val; // Update previous value / 更新上一个值
}
