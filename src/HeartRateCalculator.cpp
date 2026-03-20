#include "../include/HeartRateCalculator.h"
#include <iostream>
#include <numeric>

HeartRateCalculator::HeartRateCalculator(double sampleRateHz)
    : sample_rate_hz_(sampleRateHz),
      finger_detected_(false),
      latest_bpm_(std::nullopt),
      sample_index_(0),
      baseline_(0.0),
      smooth_(0.0),
      last_peak_time_sec_(-1.0),
      dc_alpha_(0.95),
      smooth_alpha_(0.70),
      finger_threshold_(0.001),
      peak_threshold_(0.002) {
}

void HeartRateCalculator::processSamples(const std::vector<Sample>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : samples) {
        processOne(s.ir);
    }
}

std::optional<double> HeartRateCalculator::getLatestBpm() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_bpm_;
}

bool HeartRateCalculator::fingerDetected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finger_detected_;
}

void HeartRateCalculator::processOne(float ir) {
    ++sample_index_;
    std::cout << "IR = " << ir << std::endl; //test
    finger_detected_ = (ir > finger_threshold_);

    if (!finger_detected_) {
        resetState(ir);
        return;
    }

    baseline_ = dc_alpha_ * baseline_ + (1.0 - dc_alpha_) * ir;
    double ac = ir - baseline_;

    smooth_ = smooth_alpha_ * smooth_ + (1.0 - smooth_alpha_) * ac;

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

    bool is_peak = (b > a && b > c && b > peak_threshold_);

    double now_sec = static_cast<double>(sample_index_) / sample_rate_hz_;

    if (is_peak) {
        if (last_peak_time_sec_ < 0.0) {
            last_peak_time_sec_ = now_sec;
            return;
        }

        double interval = now_sec - last_peak_time_sec_;

        if (interval >= 0.33 && interval <= 1.5) {
            double bpm = 60.0 / interval;

            bpm_window_.push_back(bpm);
            if (bpm_window_.size() > 5) {
                bpm_window_.pop_front();
            }

            double sum = std::accumulate(bpm_window_.begin(), bpm_window_.end(), 0.0);
            latest_bpm_ = sum / static_cast<double>(bpm_window_.size());
            last_peak_time_sec_ = now_sec;
        } else if (interval > 1.5) {
            last_peak_time_sec_ = now_sec;
        }
    }
}

void HeartRateCalculator::resetState(float ir) {
    baseline_ = ir;
    smooth_ = 0.0;
    last_peak_time_sec_ = -1.0;
    history_.clear();
    bpm_window_.clear();
    latest_bpm_.reset();
}