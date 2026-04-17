#ifndef HEART_RATE_CALCULATOR_H
#define HEART_RATE_CALCULATOR_H

#include "sensor.h"
#include <deque>
#include <vector>
#include <mutex>
#include <optional>
#include <functional>

class HeartRateCalculator {
public:
    explicit HeartRateCalculator(double sampleRateHz);

    void processSamples(const std::vector<Sample>& samples);
    void setBpmCallback(std::function<void(double)> cb);
    void setFingerStateCallback(std::function<void(bool)> cb);

    std::optional<double> getLatestBpm() const;
    bool fingerDetected() const;
    bool hasSamples() const;
    double getIrLevel() const;
    double getLatestRawIr() const;

private:
    enum class CallbackEventType {
        FingerState,
        Bpm
    };

    struct CallbackEvent {
        CallbackEventType type;
        bool finger_state = false;
        double bpm = 0.0;
    };

    void processOne(float ir, std::vector<CallbackEvent>& pending_events);
    void resetState(float ir);

private:
    double sample_rate_hz_;
    mutable std::mutex mutex_;
    std::function<void(double)> bpm_callback_;
    std::function<void(bool)> finger_state_callback_;

    bool finger_detected_;
    bool has_samples_;
    std::optional<double> latest_bpm_;

    size_t sample_index_;

    double baseline_;
    double smooth_;
    double last_peak_time_sec_;
    double ir_level_;
    double latest_raw_ir_;
    double signal_level_;

    std::deque<double> history_;
    std::deque<double> bpm_window_;

    double dc_alpha_;
    double smooth_alpha_;
    double ir_level_alpha_;
    double finger_enter_threshold_;
    double finger_exit_threshold_;
    double peak_threshold_;
    double signal_level_alpha_;
    double peak_slope_threshold_;
    double min_beat_interval_sec_;
    double max_beat_interval_sec_;
    double max_bpm_jump_;
    size_t finger_present_count_;
    size_t finger_absent_count_;
    size_t required_present_samples_;
    size_t required_absent_samples_;
};

#endif // HEART_RATE_CALCULATOR_H
