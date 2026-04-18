/**
 * @file Sensor.h
 * @brief MAX30102 PPG sensor driver using libgpiod for event-driven DRDY handling.
 *
 * @note Adapted from:
 *       - libgpiod official examples
 *       - MAX30102 datasheet
 */

#ifndef MAX30102_SENSOR_H
#define MAX30102_SENSOR_H

#include <gpiod.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <linux/i2c-dev.h>
#include <mutex>
#include <string>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr int DEFAULT_I2C_BUS = 1;
constexpr uint8_t DEFAULT_MAX30102_ADDRESS = 0x57;
constexpr int DEFAULT_DRDY_GPIO = 27;
constexpr int DEFAULT_DRDY_CHIP = 0;

constexpr uint8_t REG_INTR_STATUS_1 = 0x00;
constexpr uint8_t REG_INTR_STATUS_2 = 0x01;
constexpr uint8_t REG_INTR_ENABLE_1 = 0x02;
constexpr uint8_t REG_INTR_ENABLE_2 = 0x03;
constexpr uint8_t REG_FIFO_WR_PTR   = 0x04;
constexpr uint8_t REG_OVF_COUNTER   = 0x05;
constexpr uint8_t REG_FIFO_RD_PTR   = 0x06;
constexpr uint8_t REG_FIFO_DATA     = 0x07;
constexpr uint8_t REG_FIFO_CONFIG   = 0x08;
constexpr uint8_t REG_MODE_CONFIG   = 0x09;
constexpr uint8_t REG_SPO2_CONFIG   = 0x0A;
constexpr uint8_t REG_LED1_PA       = 0x0C;
constexpr uint8_t REG_LED2_PA       = 0x0D;
constexpr uint8_t REG_TEMP_CONFIG   = 0x21;
constexpr uint8_t REG_PART_ID       = 0xFF;

constexpr int FIFO_DEPTH = 32;

struct Sample {
    float red = 0.0f;
    float ir = 0.0f;
};

enum SampleAverage {
    SAMPLEAVG_1  = 0,
    SAMPLEAVG_2  = 1,
    SAMPLEAVG_4  = 2,
    SAMPLEAVG_8  = 3,
    SAMPLEAVG_16 = 4,
    SAMPLEAVG_32 = 5
};

enum SampleRate {
    SAMPLERATE_50   = 0,
    SAMPLERATE_100  = 1,
    SAMPLERATE_200  = 2,
    SAMPLERATE_400  = 3,
    SAMPLERATE_800  = 4,
    SAMPLERATE_1000 = 5,
    SAMPLERATE_1600 = 6,
    SAMPLERATE_3200 = 7
};

enum LedPulseWidth {
    PULSEWIDTH_69  = 0,
    PULSEWIDTH_118 = 1,
    PULSEWIDTH_215 = 2,
    PULSEWIDTH_411 = 3
};

/**
 * @brief Current sensor operational status.
 */
enum class SensorStatus {
    Uninitialized,
    Initializing,
    Ready,
    Running,
    Stopped,
    Error
};

class Max30102Sensor {
public:
    using DataCallback = std::function<void(const std::vector<Sample>& samples)>;

    Max30102Sensor(int interruptPin = DEFAULT_DRDY_GPIO,
                   SampleAverage avg = SAMPLEAVG_4,
                   SampleRate rate = SAMPLERATE_100,
                   LedPulseWidth width = PULSEWIDTH_411);
    ~Max30102Sensor();

    bool initialize();
    void start();
    void stop();
    void setDataCallback(DataCallback cb);

    bool checkPartID();
    bool configureSensor(SampleAverage avg = SAMPLEAVG_4,
                         SampleRate rate = SAMPLERATE_100,
                         LedPulseWidth width = PULSEWIDTH_411);

    std::vector<Sample> getLatestSamples(size_t maxCount = 100) const;
/**
     * @brief Get current sensor status (thread-safe, lock-free).
     */
    SensorStatus getStatus() const;

    /**
     * @brief Get last error message if any.
     */
    std::string getLastError() const;

private:
    void dataWorker();
    void readFifo();
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
/**
     * @brief Internal helper to update status (called from various places).
     */
    void setStatus(SensorStatus s, const std::string& err = "");


private:
    int i2c_fd_ = -1;
    int wake_fd_ = -1;
    int interrupt_pin_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    SensorStatus status_{SensorStatus::Uninitialized};
    std::string last_error_;

    std::deque<Sample> sample_buffer_;
    DataCallback data_callback_;

    SampleAverage sampleAvg_;
    SampleRate sampleRate_;
    LedPulseWidth pulseWidth_;
    std::chrono::steady_clock::time_point last_overflow_log_time_{};

    std::thread reader_thread_;

    struct gpiod_chip* chip_ = nullptr;
    struct gpiod_line_request* line_request_ = nullptr;
};

#endif // MAX30102_SENSOR_H
