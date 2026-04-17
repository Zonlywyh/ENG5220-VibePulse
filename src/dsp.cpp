#include "../include/dsp.h"
#include "../include/dsp.h"
#include <iostream>
#include <mutex>

HeartRateDSP::HeartRateDSP()
    : current_bpm(0.0f), last_peak_time(0), dc_filter_w(0.0f) {}

uint32_t HeartRateDSP::unpackRawData(uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint32_t raw = ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
    return raw & 0x3FFFF;
}

float HeartRateDSP::processSample(uint32_t raw_val)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    float current_val = static_cast<float>(raw_val);
    float prev_w = dc_filter_w;
    dc_filter_w = current_val + alpha * prev_w;
    float dc_removed = dc_filter_w - prev_w;
    static float filtered_signal = 0.0f;
    filtered_signal = filtered_signal + lpf_beta * (dc_removed - filtered_signal);
    return filtered_signal;
}

void HeartRateDSP::detectHeartRate(float filtered_val, uint32_t timestamp_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    static float last_val = 0.0f;
    static bool rising = false;
    const float threshold = 0.001f;

    if (rising && filtered_val < last_val && last_val > threshold)
    {
        if (last_peak_time != 0)
        {
            uint32_t duration = timestamp_ms - last_peak_time;
            if (duration >= 250 && duration <= 1500)
            {
                float raw_bpm = 60000.0f / static_cast<float>(duration);
                if (current_bpm < 10.0f)
                    current_bpm = raw_bpm;
                else
                    current_bpm = (current_bpm * 0.8f) + (raw_bpm * 0.2f);
            }
        }
        last_peak_time = timestamp_ms;
        rising = false;
    }
    else if (filtered_val > last_val)
    {
        rising = true;
    }
    last_val = filtered_val;
}

float HeartRateDSP::getCurrentBPM()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return current_bpm;
}

void HeartRateDSP::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    current_bpm = 0.0f;
    last_peak_time = 0;
    dc_filter_w = 0.0f;
}