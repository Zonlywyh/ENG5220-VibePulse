#include "HeartRatecalCulator.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

// 构造函数：初始化成员变量 // Constructor: initialize member variables
HeartRateCalculator::HeartRateCalculator(float sample_rate, float window_seconds)
    : sample_rate_(sample_rate)
    , window_size_(static_cast<size_t>(sample_rate * window_seconds))
    , last_peak_time_(-1.0f)
    , last_bpm_(-1.0f)
    , smoothed_bpm_(-1.0f)
    , min_peak_distance_sec_(0.3f)   // 对应最大心率200 BPM // Corresponds to max heart rate 200 BPM
    , max_peak_distance_sec_(2.0f)   // 对应最小心率30 BPM // Corresponds to min heart rate 30 BPM
{
    buffer_.reserve(window_size_);
}

// 重置内部状态 // Reset internal state
void HeartRateCalculator::reset() {
    buffer_.clear();
    peak_buffer_.clear();
    peak_values_.clear();
    last_peak_time_ = -1.0f;
    last_bpm_ = -1.0f;
    smoothed_bpm_ = -1.0f;
}

// 添加新样本，尝试计算BPM // Add a new sample and attempt to compute BPM
float HeartRateCalculator::addSample(float value) {
    // 将样本加入缓冲区 // Push sample to buffer
    buffer_.push_back(value);
    // 如果缓冲区超过窗口大小，移除最早的数据 // If buffer exceeds window size, remove oldest data
    if (buffer_.size() > window_size_) {
        buffer_.pop_front();
    }
    
    // 只有当缓冲区填满时才进行检测 // Only perform detection when buffer is full
    if (buffer_.size() == window_size_) {
        detectPeaks();           // 检测峰值 // Detect peaks
        updateBPMFromPeaks();    // 更新BPM // Update BPM
    }
    return smoothed_bpm_;        // 返回平滑后的BPM（可能为 -1.0） // Return smoothed BPM (may be -1.0)
}

// 在缓冲区中检测峰值 // Detect peaks in the buffer
void HeartRateCalculator::detectPeaks() {
    // 计算均值和标准差，用于动态阈值 // Compute mean and standard deviation for adaptive threshold
    float sum = 0.0f, sum_sq = 0.0f;
    for (auto v : buffer_) {
        sum += v;
        sum_sq += v * v;
    }
    float mean = sum / buffer_.size();
    float variance = (sum_sq / buffer_.size()) - mean * mean;
    float stddev = std::sqrt(variance);
    
    // 设置阈值为 mean + 0.5 * stddev（可根据信号质量调整） // Set threshold to mean + 0.5*stddev (adjustable based on signal quality)
    float threshold = mean + 0.5f * stddev;
    
    // 寻找局部最大值（比前后大且高于阈值） // Find local maxima (greater than neighbors and above threshold)
    for (size_t i = 1; i < buffer_.size() - 1; ++i) {
        float prev = buffer_[i-1];
        float curr = buffer_[i];
        float next = buffer_[i+1];
        
        if (curr > prev && curr > next && curr > threshold) {
            // 计算当前候选峰值的时间戳（秒） // Compute timestamp of this candidate peak in seconds
            float current_time = static_cast<float>(i) / sample_rate_;
            
            // 检查与上一个峰值的时间间隔是否过短 // Check if too close to previous peak
            if (!peak_buffer_.empty()) {
                float last_time = peak_buffer_.back();
                if (current_time - last_time < min_peak_distance_sec_) {
                    continue;   // 忽略这个候选峰值 // Ignore this candidate
                }
            }
            
            // 保存峰值信息 // Save peak information
            peak_buffer_.push_back(current_time);
            peak_values_.push_back(curr);
        }
    }
}

// 根据检测到的峰值更新BPM // Update BPM based on detected peaks
void HeartRateCalculator::updateBPMFromPeaks() {
    // 需要至少两个峰值才能计算间隔 // Need at least two peaks to compute intervals
    if (peak_buffer_.size() < 2) return;
    
    // 只保留最近5秒内的峰值（避免使用陈旧数据） // Keep only peaks within the last 5 seconds (avoid stale data)
    float current_time = static_cast<float>(buffer_.size()) / sample_rate_; // 近似当前时间 // Approximate current time
    float cutoff_time = current_time - 5.0f;
    while (!peak_buffer_.empty() && peak_buffer_.front() < cutoff_time) {
        peak_buffer_.pop_front();
        peak_values_.pop_front();
    }
    
    if (peak_buffer_.size() < 2) return;
    
    // 计算所有有效的峰间间隔 // Compute all valid inter-peak intervals
    std::vector<float> intervals;
    for (size_t i = 1; i < peak_buffer_.size(); ++i) {
        float interval_sec = peak_buffer_[i] - peak_buffer_[i-1];
        // 检查间隔是否在合理范围内 // Check if interval is within reasonable range
        if (interval_sec >= min_peak_distance_sec_ && interval_sec <= max_peak_distance_sec_) {
            intervals.push_back(interval_sec);
        }
    }
    
    if (intervals.empty()) return;
    
    // 计算平均间隔并转换为BPM // Compute average interval and convert to BPM
    float sum_interval = std::accumulate(intervals.begin(), intervals.end(), 0.0f);
    float avg_interval_sec = sum_interval / intervals.size();
    float bpm = 60.0f / avg_interval_sec;
    
    // 最终合理性检查 // Final sanity check
    if (bpm < 30.0f || bpm > 200.0f) return;
    
    // 指数移动平均平滑 // Exponential moving average smoothing
    const float alpha = 0.3f; // 平滑因子，值越小越平滑 // Smoothing factor; smaller values give more smoothing
    if (smoothed_bpm_ < 0) {
        smoothed_bpm_ = bpm;
    } else {
        smoothed_bpm_ = alpha * bpm + (1 - alpha) * smoothed_bpm_;
    }
    
    last_bpm_ = bpm;
}
