#include "../include/HeartRateCalculator.h"

#include <algorithm>
#include <cmath>
#include <numeric>

HeartRateCalculator::HeartRateCalculator(double sampleRateHz, HeartRateConfig config)
    : sample_rate_hz_(sampleRateHz),
      config_(std::move(config)),
      finger_detected_(false),
      has_samples_(false),
      latest_bpm_(std::nullopt),
      sample_index_(0),
      baseline_(0.0),
      smooth_(0.0),
      last_peak_time_sec_(-1.0),
      ir_level_(0.0),
      latest_raw_ir_(0.0),
      signal_level_(0.0),
      finger_present_count_(0),
      finger_absent_count_(0),
      peak_history_count_(0),
      peak_history_next_index_(0) {
    bpm_window_.reserve(config_.bpm_window_size);
}

void HeartRateCalculator::processIrSample(float ir) {
    PendingCallbacks pending_callbacks;
    std::function<void(double)> bpm_callback;
    std::function<void(bool)> finger_state_callback;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        processIrSampleLocked(ir, pending_callbacks);
        bpm_callback = bpm_callback_;
        finger_state_callback = finger_state_callback_;
    }

    if (pending_callbacks.finger_state_changed.has_value() && finger_state_callback) {
        finger_state_callback(*pending_callbacks.finger_state_changed);
    }
    if (pending_callbacks.bpm_updated.has_value() && bpm_callback) {
        bpm_callback(*pending_callbacks.bpm_updated);
    }
}

void HeartRateCalculator::processIrSamples(const std::vector<float>& irSamples) {
    for (float ir : irSamples) {
        processIrSample(ir);
    }
}

void HeartRateCalculator::setBpmCallback(std::function<void(double)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    bpm_callback_ = std::move(cb);
}

void HeartRateCalculator::setFingerStateCallback(std::function<void(bool)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    finger_state_callback_ = std::move(cb);
}

std::optional<double> HeartRateCalculator::getLatestBpm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_bpm_;
}

bool HeartRateCalculator::fingerDetected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finger_detected_;
}

bool HeartRateCalculator::hasSamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_samples_;
}

double HeartRateCalculator::getIrLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ir_level_;
}

double HeartRateCalculator::getLatestRawIr() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_raw_ir_;
}

void HeartRateCalculator::processIrSampleLocked(float ir, PendingCallbacks& pending_callbacks) {
    ++sample_index_;
    has_samples_ = true;
    latest_raw_ir_ = ir;
    ir_level_ = config_.ir_level_alpha * ir_level_ + (1.0 - config_.ir_level_alpha) * ir;

    updateFingerStateLocked(ir, pending_callbacks);

    if (!finger_detected_) {
        resetState(ir);
        return;
    }

    updateSignalModelLocked(ir);
    pending_callbacks.bpm_updated = tryDetectBeatLocked();
}

void HeartRateCalculator::updateFingerStateLocked(float ir, PendingCallbacks& pending_callbacks) {
    if (ir_level_ >= config_.finger_enter_threshold) {
        ++finger_present_count_;
        finger_absent_count_ = 0;
    } else if (ir_level_ <= config_.finger_exit_threshold) {
        ++finger_absent_count_;
        finger_present_count_ = 0;
    }

    if (!finger_detected_ && finger_present_count_ >= config_.required_present_samples) {
        finger_detected_ = true;
        finger_absent_count_ = 0;
        pending_callbacks.finger_state_changed = true;
        baseline_ = ir;
        smooth_ = 0.0;
        peak_history_count_ = 0;
        peak_history_next_index_ = 0;
        bpm_window_.clear();
        latest_bpm_.reset();
        last_peak_time_sec_ = -1.0;
    } else if (finger_detected_ && finger_absent_count_ >= config_.required_absent_samples) {
        finger_detected_ = false;
        finger_present_count_ = 0;
        pending_callbacks.finger_state_changed = false;
    }
}

void HeartRateCalculator::updateSignalModelLocked(float ir) {
    baseline_ = config_.dc_alpha * baseline_ + (1.0 - config_.dc_alpha) * ir;
    const double ac = ir - baseline_;

    smooth_ = config_.smooth_alpha * smooth_ + (1.0 - config_.smooth_alpha) * ac;
    signal_level_ = config_.signal_level_alpha * signal_level_ +
                    (1.0 - config_.signal_level_alpha) * std::fabs(smooth_);

    peak_history_[peak_history_next_index_] = smooth_;
    peak_history_next_index_ = (peak_history_next_index_ + 1U) % peak_history_.size();
    if (peak_history_count_ < peak_history_.size()) {
        ++peak_history_count_;
    }
}

std::optional<double> HeartRateCalculator::tryDetectBeatLocked() {
    if (peak_history_count_ < peak_history_.size()) {
        return std::nullopt;
    }

    const size_t oldest_index = peak_history_next_index_;
    const size_t middle_index = (peak_history_next_index_ + 1U) % peak_history_.size();
    const size_t newest_index = (peak_history_next_index_ + 2U) % peak_history_.size();

    const double a = peak_history_[oldest_index];
    const double b = peak_history_[middle_index];
    const double c = peak_history_[newest_index];
    const double dynamic_peak_threshold = std::max(config_.peak_threshold, signal_level_ * 0.6);
    const bool rising_then_falling =
        ((b - a) > config_.peak_slope_threshold) && ((b - c) > config_.peak_slope_threshold);
    const bool local_peak = (b > a && b > c);
    const bool is_peak = (local_peak && b > dynamic_peak_threshold && rising_then_falling);

    if (!is_peak) {
        return std::nullopt;
    }

    const double now_sec = static_cast<double>(sample_index_) / sample_rate_hz_;

    if (last_peak_time_sec_ < 0.0) {
        last_peak_time_sec_ = now_sec;
        return std::nullopt;
    }

    const double interval = now_sec - last_peak_time_sec_;

    if (interval >= config_.min_beat_interval_sec && interval <= config_.max_beat_interval_sec) {
        const double bpm = 60.0 / interval;

        if (latest_bpm_.has_value() && std::fabs(bpm - *latest_bpm_) > config_.max_bpm_jump) {
            last_peak_time_sec_ = now_sec;
            return std::nullopt;
        }

        bpm_window_.push_back(bpm);
        if (bpm_window_.size() > config_.bpm_window_size) {
            bpm_window_.erase(bpm_window_.begin());
        }

        const double sum = std::accumulate(bpm_window_.begin(), bpm_window_.end(), 0.0);
        latest_bpm_ = sum / static_cast<double>(bpm_window_.size());
        last_peak_time_sec_ = now_sec;
        return latest_bpm_;
    }

    if (interval > config_.max_beat_interval_sec) {
        last_peak_time_sec_ = now_sec;
    }

    return std::nullopt;
}

void HeartRateCalculator::resetState(float ir) {
    baseline_ = ir;
    smooth_ = 0.0;
    last_peak_time_sec_ = -1.0;
    signal_level_ = 0.0;
    peak_history_count_ = 0;
    peak_history_next_index_ = 0;
    bpm_window_.clear();
    latest_bpm_.reset();
}
