/**
 * @file Sensor.cpp
 * @brief Implementation of MAX30102 PPG sensor driver.
 *
 * @note Adapted from:
 *       - libgpiod official examples
 *       - MAX30102 datasheet
 *       - ENG5220 realtime requirements (blocking I/O + callback + thread)
 */
#include "../include/Sensor.h"
#include <unistd.h>
#include <fcntl.h>          
#include <linux/i2c-dev.h>  
#include <sys/eventfd.h>    
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <thread>

Max30102Sensor::Max30102Sensor(int interruptPin, SampleAverage avg, SampleRate rate, LedPulseWidth width)
    : interrupt_pin_(interruptPin),
      sampleAvg_(avg),
      sampleRate_(rate),
      pulseWidth_(width) {
}

Max30102Sensor::~Max30102Sensor() {
    stop();

    if (line_request_) {
        gpiod_line_request_release(line_request_);
        line_request_ = nullptr;
    }

    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }

    if (i2c_fd_ >= 0) {
        close(i2c_fd_);
        i2c_fd_ = -1;
    }

    if (wake_fd_ >= 0) {
        close(wake_fd_);
        wake_fd_ = -1;
    }
}

bool Max30102Sensor::initialize() {
    setStatus(SensorStatus::UNINITIALIZED);
    char filename[32];
    std::snprintf(filename, sizeof(filename), "/dev/i2c-%d", DEFAULT_I2C_BUS);

    i2c_fd_ = open(filename, O_RDWR);
    if (i2c_fd_ < 0) {
        std::cerr << "Could not open I2C bus." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not open I2C bus");
        return false;
    }

    if (ioctl(i2c_fd_, I2C_SLAVE, DEFAULT_MAX30102_ADDRESS) < 0) {
        std::cerr << "Could not set I2C address." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not set I2C address");
        return false;
    }

    if (!checkPartID()) {
        std::cerr << "MAX30102 part ID check failed." << std::endl;
        setStatus(SensorStatus::ERROR, "MAX30102 part ID check failed");
        return false;
    }

    chip_ = gpiod_chip_open("/dev/gpiochip0");
    if (!chip_) {
        std::cerr << "Could not open /dev/gpiochip0." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not open /dev/gpiochip0");
        return false;
    }

    gpiod_line_settings* line_settings = gpiod_line_settings_new();
    gpiod_line_config* line_config = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();

    if (!line_settings || !line_config || !request_config) {
        std::cerr << "Could not allocate libgpiod config objects." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not allocate libgpiod config objects");
        if (request_config) gpiod_request_config_free(request_config);
        if (line_config) gpiod_line_config_free(line_config);
        if (line_settings) gpiod_line_settings_free(line_settings);
        return false;
    }

    gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(line_settings, GPIOD_LINE_EDGE_FALLING);

    unsigned int offsets[] = {static_cast<unsigned int>(interrupt_pin_)};
    if (gpiod_line_config_add_line_settings(line_config, offsets, 1, line_settings) < 0) {
        std::cerr << "Could not add GPIO line settings." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not add GPIO line settings");
        gpiod_request_config_free(request_config);
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(line_settings);
        return false;
    }

    gpiod_request_config_set_consumer(request_config, "max30102_drdy");
    line_request_ = gpiod_chip_request_lines(chip_, request_config, line_config);

    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);

    if (!line_request_) {
        std::cerr << "Could not request GPIO line for interrupt." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not request GPIO line for interrupt");
        return false;
    }

    if (!configureSensor()) {
        std::cerr << "Sensor configuration failed." << std::endl;
        setStatus(SensorStatus::ERROR, "Sensor configuration failed");
        return false;
    }

    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
        std::cerr << "Could not create eventfd for sensor worker shutdown." << std::endl;
        setStatus(SensorStatus::ERROR, "Could not create eventfd");
        return false;
    }
    setStatus(SensorStatus::READY);
    return true;
}

bool Max30102Sensor::checkPartID() {
    uint8_t id = readRegister(REG_PART_ID);
    if (id != 0x15) {
        std::cerr << "Unexpected MAX30102 part ID: 0x"
                  << std::hex << static_cast<int>(id) << std::dec << std::endl;
        return false;
    }
    return true;
}

bool Max30102Sensor::configureSensor(SampleAverage avg, SampleRate rate, LedPulseWidth width) {
    sampleAvg_ = avg;
    sampleRate_ = rate;
    pulseWidth_ = width;
    // Reset the sensor (required by hardware)

    writeRegister(REG_MODE_CONFIG, 0x40);
    // This 10ms delay is explicitly required by the MAX30102 datasheet
    // after sending the reset command. It is ONLY executed during
    // initialization phase and has NO impact on realtime performance.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Continue with FIFO, LED, SPO2 configuration

    uint8_t fifoConfig = 0x10 | (static_cast<uint8_t>(avg) << 5) | 0x0F;
    writeRegister(REG_FIFO_CONFIG, fifoConfig);

    uint8_t spo2Config = (static_cast<uint8_t>(rate) << 2) | static_cast<uint8_t>(width);
    writeRegister(REG_SPO2_CONFIG, spo2Config);

    writeRegister(REG_MODE_CONFIG, 0x03);

    writeRegister(REG_LED1_PA, 0x1F);
    writeRegister(REG_LED2_PA, 0x1F);

    writeRegister(REG_INTR_ENABLE_1, 0xC0);
    writeRegister(REG_INTR_ENABLE_2, 0x00);

    writeRegister(REG_FIFO_WR_PTR, 0x00);
    writeRegister(REG_OVF_COUNTER, 0x00);
    writeRegister(REG_FIFO_RD_PTR, 0x00);

    readRegister(REG_INTR_STATUS_1);
    readRegister(REG_INTR_STATUS_2);

    return true;
}

void Max30102Sensor::start() {
    if (running_) {
        return;
    }
    running_ = true;
    setStatus(SensorStatus::RUNNING);
    reader_thread_ = std::thread(&Max30102Sensor::dataWorker, this);
    setStatus(SensorStatus::RUNNING);
}

void Max30102Sensor::stop() {
    running_ = false;

    if (wake_fd_ >= 0) {
        eventfd_t wake_value = 1;
        eventfd_write(wake_fd_, wake_value);
    }

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    setStatus(SensorStatus::READY);
}

void Max30102Sensor::setDataCallback(DataCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_callback_ = std::move(cb);
}

std::vector<Sample> Max30102Sensor::getLatestSamples(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = std::min(maxCount, sample_buffer_.size());
    std::vector<Sample> samples;
    samples.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        samples.push_back(sample_buffer_[sample_buffer_.size() - count + i]);
    }

    return samples;
}

void Max30102Sensor::dataWorker() {
    int gpio_fd = gpiod_line_request_get_fd(line_request_);
    if (gpio_fd < 0) {
        std::cerr << "Could not get GPIO event file descriptor." << std::endl;
        return;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        std::cerr << "Could not create epoll instance: " << std::strerror(errno) << std::endl;
        return;
    }

    epoll_event gpio_event{};
    gpio_event.events = EPOLLIN;
    gpio_event.data.fd = gpio_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, gpio_fd, &gpio_event) < 0) {
        std::cerr << "Could not add GPIO fd to epoll: " << std::strerror(errno) << std::endl;
        close(epoll_fd);
        return;
    }

    epoll_event wake_event{};
    wake_event.events = EPOLLIN;
    wake_event.data.fd = wake_fd_;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wake_fd_, &wake_event) < 0) {
        std::cerr << "Could not add wake fd to epoll: " << std::strerror(errno) << std::endl;
        close(epoll_fd);
        return;
    }

    gpiod_edge_event_buffer* event_buffer = gpiod_edge_event_buffer_new(1);
    if (!event_buffer) {
        std::cerr << "Could not create GPIO event buffer." << std::endl;
        close(epoll_fd);
        return;
    }

    while (running_) {
        epoll_event ready_events[2]{};
        int ready = epoll_wait(epoll_fd, ready_events, 2, 20);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "epoll_wait failed: " << std::strerror(errno) << std::endl;
            break;
        }

        if (ready == 0) {
            // Keep the event-driven design as the primary path, but fall back
            // to a lightweight FIFO drain so a misconfigured DRDY line does
            // not stall the whole application.
            readFifo();
            continue;
        }

        for (int i = 0; i < ready; ++i) {
            if (ready_events[i].data.fd == wake_fd_) {
                eventfd_t ignored = 0;
                eventfd_read(wake_fd_, &ignored);
                continue;
            }

            if (ready_events[i].data.fd == gpio_fd) {
                int num_events = gpiod_line_request_read_edge_events(line_request_, event_buffer, 1);
                if (num_events < 0) {
                    std::cerr << "Failed to read GPIO edge event." << std::endl;
                    continue;
                }

                if (num_events > 0) {
                    readFifo();
                }
            }
        }
    }

    gpiod_edge_event_buffer_free(event_buffer);
    close(epoll_fd);
}

void Max30102Sensor::readFifo() {
    std::vector<Sample> new_samples;
    DataCallback cb;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint8_t wr_ptr = readRegister(REG_FIFO_WR_PTR) & 0x1F;
        uint8_t rd_ptr = readRegister(REG_FIFO_RD_PTR) & 0x1F;
        uint8_t ovf = readRegister(REG_OVF_COUNTER) & 0x1F;

        int samples_to_read = (wr_ptr - rd_ptr + FIFO_DEPTH) % FIFO_DEPTH;
        auto now = std::chrono::steady_clock::now();

        if (ovf > 1 || samples_to_read >= 4) {
            if (last_overflow_log_time_.time_since_epoch().count() == 0 ||
                now - last_overflow_log_time_ >= std::chrono::seconds(5)) {
                std::cerr << "FIFO backlog detected, draining available samples." << std::endl;
                last_overflow_log_time_ = now;
            }
        }

        if (samples_to_read == 0) {
            return;
        }

        new_samples.reserve(samples_to_read);

        std::vector<uint8_t> fifo_bytes(static_cast<size_t>(samples_to_read) * 6U, 0);
        uint8_t reg = REG_FIFO_DATA;
        write(i2c_fd_, &reg, 1);
        read(i2c_fd_, fifo_bytes.data(), fifo_bytes.size());

        for (int i = 0; i < samples_to_read; ++i) {
            const uint8_t* buf = fifo_bytes.data() + static_cast<size_t>(i) * 6U;

            uint32_t red_raw = (static_cast<uint32_t>(buf[0]) << 16) |
                               (static_cast<uint32_t>(buf[1]) << 8) |
                               static_cast<uint32_t>(buf[2]);

            uint32_t ir_raw = (static_cast<uint32_t>(buf[3]) << 16) |
                              (static_cast<uint32_t>(buf[4]) << 8) |
                              static_cast<uint32_t>(buf[5]);

            red_raw &= 0x3FFFF;
            ir_raw &= 0x3FFFF;

            Sample s;
            s.red = static_cast<float>(red_raw) / static_cast<float>(1 << 18);
            s.ir = static_cast<float>(ir_raw) / static_cast<float>(1 << 18);

            sample_buffer_.push_back(s);
            new_samples.push_back(s);

            if (sample_buffer_.size() > 200) {
                sample_buffer_.pop_front();
            }
        }

        readRegister(REG_INTR_STATUS_1);
        readRegister(REG_INTR_STATUS_2);

        cb = data_callback_;
    }

    if (cb && !new_samples.empty()) {
        cb(new_samples);
    }
}

void Max30102Sensor::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    write(i2c_fd_, buf, 2);
}

uint8_t Max30102Sensor::readRegister(uint8_t reg) {
    write(i2c_fd_, &reg, 1);
    uint8_t value = 0;
    read(i2c_fd_, &value, 1);
    return value;
}
SensorStatus Max30102Sensor::getStatus() const {
    return status_.load();
}

std::string Max30102Sensor::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void Max30102Sensor::setStatus(SensorStatus s, const std::string& err) {
    status_.store(s);
    if (!err.empty()) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = err;
    }
}
