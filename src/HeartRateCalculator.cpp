#include "../include/HeartRateCalculator.h"
#include <iostream>
#include <cmath>
#include <numeric>

HeartRateCalculator::HeartRateCalculator(double sampleRateHz)
    : sample_rate_hz_(sampleRateHz),
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
      dc_alpha_(0.95),
      smooth_alpha_(0.70),
      ir_level_alpha_(0.92),
      finger_enter_threshold_(0.05),
      finger_exit_threshold_(0.02),
      peak_threshold_(0.0010),
      signal_level_alpha_(0.92),
      peak_slope_threshold_(0.00015),
      min_beat_interval_sec_(0.50),
      max_beat_interval_sec_(1.5),
      max_bpm_jump_(20.0),
      finger_present_count_(0),
      finger_absent_count_(0),
      required_present_samples_(6),
      required_absent_samples_(12) {
}

void HeartRateCalculator::processSamples(const std::vector<Sample>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : samples) {
        processOne(s.ir);
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

void HeartRateCalculator::processOne(float ir) {
    ++sample_index_;
    has_samples_ = true;
    latest_raw_ir_ = ir;
    ir_level_ = ir_level_alpha_ * ir_level_ + (1.0 - ir_level_alpha_) * ir;

    if (ir_level_ >= finger_enter_threshold_) {
        ++finger_present_count_;
        finger_absent_count_ = 0;
    } else if (ir_level_ <= finger_exit_threshold_) {
        ++finger_absent_count_;
        finger_present_count_ = 0;
    }

    if (!finger_detected_ && finger_present_count_ >= required_present_samples_) {
        finger_detected_ = true;
        if (finger_state_callback_) {
            finger_state_callback_(true);
        }
        baseline_ = ir;
        smooth_ = 0.0;
        history_.clear();
        bpm_window_.clear();
        latest_bpm_.reset();
        last_peak_time_sec_ = -1.0;
    } else if (finger_detected_ && finger_absent_count_ >= required_absent_samples_) {
        finger_detected_ = false;
        if (finger_state_callback_) {
            finger_state_callback_(false);
        }
    }

    if (!finger_detected_) {
        resetState(ir);
        return;
    }

    baseline_ = dc_alpha_ * baseline_ + (1.0 - dc_alpha_) * ir;
    double ac = ir - baseline_;

    smooth_ = smooth_alpha_ * smooth_ + (1.0 - smooth_alpha_) * ac;
    signal_level_ = signal_level_alpha_ * signal_level_ +
                    (1.0 - signal_level_alpha_) * std::fabs(smooth_);

    history_.push_back(smooth_);
    if (history_.size() < 3) {
        return;
    }
    if (history_.size() > 3) {
        history_.pop_front();
    }

    double a = history_[0];
    double b = history_[1];
    double c = history_[2];
    double dynamic_peak_threshold = std::max(peak_threshold_, signal_level_ * 0.6);
    bool rising_then_falling = ((b - a) > peak_slope_threshold_) && ((b - c) > peak_slope_threshold_);
    bool local_peak = (b > a && b > c);

    bool is_peak = (local_peak && b > dynamic_peak_threshold && rising_then_falling);

    double now_sec = static_cast<double>(sample_index_) / sample_rate_hz_;

    if (is_peak) {
        if (last_peak_time_sec_ < 0.0) {
            last_peak_time_sec_ = now_sec;
            return;
        }

        double interval = now_sec - last_peak_time_sec_;

        if (interval >= min_beat_interval_sec_ && interval <= max_beat_interval_sec_) {
            double bpm = 60.0 / interval;

            if (latest_bpm_.has_value() && std::fabs(bpm - *latest_bpm_) > max_bpm_jump_) {
                last_peak_time_sec_ = now_sec;
                return;
            }

            bpm_window_.push_back(bpm);
            if (bpm_window_.size() > 5) {
                bpm_window_.pop_front();
            }

            double sum = std::accumulate(bpm_window_.begin(), bpm_window_.end(), 0.0);
            latest_bpm_ = sum / static_cast<double>(bpm_window_.size());
            last_peak_time_sec_ = now_sec;
            if (bpm_callback_) {
                bpm_callback_(*latest_bpm_);
            }
        } else if (interval > max_beat_interval_sec_) {
            last_peak_time_sec_ = now_sec;
        }
    }
}

void HeartRateCalculator::resetState(float ir) {
    baseline_ = ir;
    smooth_ = 0.0;
    last_peak_time_sec_ = -1.0;
    signal_level_ = 0.0;
    history_.clear();
    bpm_window_.clear();
    latest_bpm_.reset();
}
