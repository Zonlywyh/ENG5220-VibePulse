#ifndef HEART_RATE_CALCULATOR_H
#define HEART_RATE_CALCULATOR_H

#include "sensor.h"
#include <deque>
#include <vector>
#include <mutex>
#include <optional>

class HeartRateCalculator {
public:
    explicit HeartRateCalculator(double sampleRateHz);

    void processSamples(const std::vector<Sample>& samples);

    std::optional<double> getLatestBpm() const;
    bool fingerDetected() const;

private:
    void processOne(float ir);
    void resetState(float ir);

private:
    double sample_rate_hz_;
    mutable std::mutex mutex_;

    bool finger_detected_;
    std::optional<double> latest_bpm_;

    size_t sample_index_;

    double baseline_;
    double smooth_;
    double last_peak_time_sec_;

    std::deque<double> history_;
    std::deque<double> bpm_window_;

    double dc_alpha_;
    double smooth_alpha_;
    double finger_threshold_;
    double peak_threshold_;
};

#endif // HEART_RATE_CALCULATOR_H
