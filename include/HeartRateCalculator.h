#ifndef HEART_RATE_CALCULATOR_H
#define HEART_RATE_CALCULATOR_H

#include <deque>
#include <vector>
#include <cstdint>

/**
 * 心率计算器类，用于从预处理后的PPG信号中提取心率（BPM）
 * Heart rate calculator class for extracting heart rate (BPM) from preprocessed PPG signal.
 */
class HeartRateCalculator {
public:
    /**
     * 构造函数
     * Constructor.
     * @param sample_rate     采样率（Hz） // Sampling rate in Hz
     * @param window_seconds  用于分析的数据窗口长度（秒） // Data window length in seconds for analysis
     */
    HeartRateCalculator(float sample_rate, float window_seconds = 6.0f);
    
    /**
     * 添加一个新样本，如果BPM更新则返回新值，否则返回 -1.0
     * Add a new sample; returns updated BPM if available, otherwise -1.0.
     * @param value  预处理后的信号样本值 // Preprocessed signal sample value
     * @return       当前估计的BPM，或 -1.0 // Current estimated BPM, or -1.0
     */
    float addSample(float value);
    
    /**
     * 获取当前平滑后的BPM值（只读）
     * Get the current smoothed BPM value (read-only).
     * @return 平滑后的BPM // Smoothed BPM
     */
    float getBPM() const { return smoothed_bpm_; }
    
    /**
     * 重置计算器的内部状态（例如切换用户时）
     * Reset the internal state of the calculator (e.g., when switching users).
     */
    void reset();

private:
    float sample_rate_;                 // 采样率 (Hz) // Sampling rate
    size_t window_size_;                 // 数据窗口内的样本数 // Number of samples in the data window
    std::deque<float> buffer_;           // 存储最近的数据样本 // Buffer of recent data samples
    
    // 峰值检测相关 // Peak detection related
    std::deque<float> peak_buffer_;      // 检测到的峰值的时间戳（秒） // Timestamps of detected peaks (seconds)
    std::deque<float> peak_values_;      // 检测到的峰值的幅度 // Amplitude values of detected peaks
    
    float last_peak_time_;               // 上一个峰值的时间（秒） // Time of last peak (seconds)
    float last_bpm_;                     // 最近一次计算的BPM // Last computed BPM
    float smoothed_bpm_;                 // 平滑后的BPM输出 // Smoothed BPM output
    
    // 可调参数 // Tunable parameters
    float min_peak_distance_sec_;         // 最小峰间距（秒），对应最高心率限制 // Minimum peak-to-peak distance (seconds), corresponds to max heart rate limit
    float max_peak_distance_sec_;         // 最大峰间距（秒），对应最低心率限制 // Maximum peak-to-peak distance (seconds), corresponds to min heart rate limit
    
    // 辅助函数 // Helper functions
    /**
     * 在缓冲区中检测峰值
     * Detect peaks in the current buffer.
     */
    void detectPeaks();
    
    /**
     * 根据检测到的峰值更新BPM
     * Update BPM based on detected peaks.
     */
    void updateBPMFromPeaks();
};

#endif // HEART_RATE_CALCULATOR_H
