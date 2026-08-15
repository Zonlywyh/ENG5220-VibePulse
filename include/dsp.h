#ifndef DSP_H
#define DSP_H

#include <mutex>
#include <cstdint>

class HeartRateDSP
{
public:
    HeartRateDSP();

    uint32_t unpackRawData(uint8_t b1, uint8_t b2, uint8_t b3);
    float processSample(uint32_t raw_val);
    void detectHeartRate(float filtered_val, uint32_t timestamp_ms);
    float getCurrentBPM();
    void reset();

private:
    mutable std::mutex m_mutex;
    float current_bpm;
    uint32_t last_peak_time;
    float dc_filter_w;

    const float alpha = 0.95f;
    const float lpf_beta = 0.1f;
};

#endif
