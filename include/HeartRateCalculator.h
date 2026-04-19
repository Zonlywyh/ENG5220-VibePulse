#ifndef HEART_RATE_CALCULATOR_H
#define HEART_RATE_CALCULATOR_H

#include <array>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

struct HeartRateConfig {
    double dc_alpha = 0.95;
    double smooth_alpha = 0.70;
    double ir_level_alpha = 0.92;
    double finger_enter_threshold = 0.05;
    double finger_exit_threshold = 0.02;
    double peak_threshold = 0.0010;
    double signal_level_alpha = 0.92;
    double peak_slope_threshold = 0.00015;
    double min_beat_interval_sec = 0.50;
    double max_beat_interval_sec = 1.5;
    double max_bpm_jump = 20.0;
    size_t required_present_samples = 6;
    size_t required_absent_samples = 12;
    size_t bpm_window_size = 5;
};

class HeartRateCalculator {
public:
    /**
     * Creates a heart-rate calculator for a fixed input sampling frequency.
     *
     * @param sampleRateHz Number of IR samples processed per second.
     */
    explicit HeartRateCalculator(double sampleRateHz, HeartRateConfig config = {});

    /**
     * Processes one IR sample from the sensor.
     *
     * @param ir Normalized IR sample value.
     */
    void processIrSample(float ir);

    /**
     * Processes a batch of IR samples in capture order.
     *
     * @param irSamples Sequence of normalized IR sample values.
     */
    void processIrSamples(const std::vector<float>& irSamples);

    /**
     * Registers a callback fired whenever a new BPM estimate is available.
     *
     * @param cb Callback receiving the filtered BPM estimate.
     */
    void setBpmCallback(std::function<void(double)> cb);

    /**
     * Registers a callback fired when finger presence changes.
     *
     * @param cb Callback receiving true when a finger is detected.
     */
    void setFingerStateCallback(std::function<void(bool)> cb);

    /**
     * Returns the most recent BPM estimate, if available.
     */
    std::optional<double> getLatestBpm() const;

    /**
     * Returns whether a finger is currently considered present.
     */
    bool fingerDetected() const;

    /**
     * Returns whether any IR sample has been processed.
     */
    bool hasSamples() const;

    /**
     * Returns the smoothed IR level used for finger detection.
     */
    double getIrLevel() const;

    /**
     * Returns the latest raw IR sample processed by the estimator.
     */
    double getLatestRawIr() const;

private:
    struct PendingCallbacks {
        std::optional<bool> finger_state_changed;
        std::optional<double> bpm_updated;
    };

    void processIrSampleLocked(float ir, PendingCallbacks& pendingCallbacks);
    void updateFingerStateLocked(float ir, PendingCallbacks& pendingCallbacks);
    void updateSignalModelLocked(float ir);
    std::optional<double> tryDetectBeatLocked();
    void resetState(float ir);

private:
    double sample_rate_hz_;
    HeartRateConfig config_;
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

    std::array<double, 3> peak_history_{};
    size_t peak_history_count_;
    size_t peak_history_next_index_;
    std::vector<double> bpm_window_;

    size_t finger_present_count_;
    size_t finger_absent_count_;
};

#endif // HEART_RATE_CALCULATOR_H
