#include "Max30102Sensor.h"
#include <iostream>  // For debug output / 用于调试输出

// Constructor / 构造函数
Max30102Sensor::Max30102Sensor(int interruptPin)
    : interrupt_pin_(interruptPin) {}

// Destructor: ensure stop / 析构函数：确保停止
Max30102Sensor::~Max30102Sensor() {
    stop();
    if (i2c_fd_ >= 0) close(i2c_fd_);
    if (line_irq_) gpiod_line_release(line_irq_);
    if (chip_) gpiod_chip_close(chip_);
}

// Initialize: open I2C and GPIO / 初始化：打开I2C和GPIO
bool Max30102Sensor::initialize() {
    // Open I2C / 打开I2C
    char filename[20];
    snprintf(filename, sizeof(filename), "/dev/i2c-%d", DEFAULT_I2C_BUS);
    i2c_fd_ = open(filename, O_RDWR);
    if (i2c_fd_ < 0) {
        std::cerr << "Could not open I2C." << std::endl;
        return false;
    }
    if (ioctl(i2c_fd_, I2C_SLAVE, DEFAULT_MAX30102_ADDRESS) < 0) {
        std::cerr << "Could not set I2C address." << std::endl;
        return false;
    }

    // Open GPIO chip / 打开GPIO芯片
    chip_ = gpiod_chip_open_by_number(DEFAULT_DRDY_CHIP);
    if (!chip_) {
        std::cerr << "Could not open GPIO chip." << std::endl;
        return false;
    }
    line_irq_ = gpiod_chip_get_line(chip_, interrupt_pin_);
    if (!line_irq_) {
        std::cerr << "Could not get GPIO line." << std::endl;
        return false;
    }
    if (gpiod_line_request_falling_edge_events(line_irq_, "max30102_drdy") < 0) {
        std::cerr << "Could not request falling edge events." << std::endl;
        return false;
    }

    // Configure sensor / 配置传感器
    if (!configureSensor()) return false;

    return true;
}

// Configure registers (from repo) / 配置寄存器（来自仓库）
bool Max30102Sensor::configureSensor() {
    // Reset / 复位
    writeRegister(REG_MODE_CONFIG, 0x40);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Reset delay (init only) / 复位延迟（仅初始化）

    // Mode: SpO2 + HR / 模式：SpO2 + HR
    writeRegister(REG_MODE_CONFIG, 0x03);

    // FIFO: avg 4 samples, rollover / FIFO：平均4样本，滚动
    writeRegister(REG_FIFO_CONFIG, 0x40 | 0x0F);

    // SpO2 config: 100Hz, 411us pulse / SpO2配置：100Hz，411us脉冲
    writeRegister(REG_SPO2_CONFIG, 0x27);

    // LED currents / LED电流
    writeRegister(REG_LED1_PA, 0x1F);  // Red ~7.6mA / 红色~7.6mA
    writeRegister(REG_LED2_PA, 0x1F);  // IR ~7.6mA / 红外~7.6mA

    // Enable A_FULL interrupt / 启用A_FULL中断
    writeRegister(REG_INTR_ENABLE_1, 0xC0);

    // Clear FIFO pointers / 清FIFO指针
    writeRegister(REG_FIFO_WR_PTR, 0x00);
    writeRegister(REG_OVF_COUNTER, 0x00);
    writeRegister(REG_FIFO_RD_PTR, 0x00);

    return true;
}

// Start: launch thread / 开始：启动线程
void Max30102Sensor::start() {
    if (running_) return;
    running_ = true;
    reader_thread_ = std::thread(&Max30102Sensor::dataWorker, this);
}

// Stop: join thread / 停止：加入线程
void Max30102Sensor::stop() {
    running_ = false;
    if (reader_thread_.joinable()) reader_thread_.join();
}

// Set callback / 设置回调
void Max30102Sensor::setDataCallback(DataCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_callback_ = cb;
}

// Get latest samples / 获取最新样本
std::vector<Sample> Max30102Sensor::getLatestSamples(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = std::min(maxCount, sample_buffer_.size());
    std::vector<Sample> samples(sample_buffer_.rbegin(), sample_buffer_.rbegin() + count);
    return samples;
}

// Data worker thread: block on event / 数据工作线程：阻塞等待事件
void Max30102Sensor::dataWorker() {
    struct timespec timeout = {1, 0};  // 1s timeout / 1秒超时
    while (running_) {
        int ret = gpiod_line_event_wait(line_irq_, &timeout);
        if (ret > 0) {
            struct gpiod_line_event event;
            gpiod_line_event_read(line_irq_, &event);  // Read event / 读取事件
            readFifo();  // Process data / 处理数据
        } else if (ret < 0) {
            std::cerr << "Event wait error." << std::endl;
        }
    }
}

// Read FIFO (burst read multiple samples) / 读取FIFO（批量读取多个样本）
void Max30102Sensor::readFifo() {
    std::lock_guard<std::mutex> lock(mutex_);

    uint8_t wr_ptr = readRegister(REG_FIFO_WR_PTR);
    uint8_t rd_ptr = readRegister(REG_FIFO_RD_PTR);
    uint8_t ovf = readRegister(REG_OVF_COUNTER);

    if (ovf > 0) {
        std::cerr << "FIFO overflow!" << std::endl;
        writeRegister(REG_FIFO_WR_PTR, 0x00);
        writeRegister(REG_OVF_COUNTER, 0x00);
        writeRegister(REG_FIFO_RD_PTR, 0x00);
        return;
    }

    int samples_to_read = (wr_ptr - rd_ptr + FIFO_DEPTH) % FIFO_DEPTH;
    if (samples_to_read == 0) return;

    std::vector<Sample> new_samples;
    new_samples.reserve(samples_to_read);

    for (int i = 0; i < samples_to_read; ++i) {
        uint8_t buf[6];
        writeRegister(REG_FIFO_DATA, 0x00);  // Set pointer / 设置指针
        read(i2c_fd_, buf, 6);  // Burst read 6 bytes / 批量读取6字节

        uint32_t red_raw = (buf[0] << 16) | (buf[1] << 8) | buf[2];
        uint32_t ir_raw = (buf[3] << 16) | (buf[4] << 8) | buf[5];
        red_raw &= 0x3FFFF;  // 18-bit / 18位
        ir_raw &= 0x3FFFF;

        Sample s;
        s.red = static_cast<float>(red_raw) / (1 << 18);  // Normalize [0,1] / 归一化[0,1]
        s.ir = static_cast<float>(ir_raw) / (1 << 18);

        sample_buffer_.push_back(s);
        new_samples.push_back(s);

        if (sample_buffer_.size() > 200) sample_buffer_.pop_front();  // Limit size / 限制大小
    }

    // Update read pointer / 更新读取指针
    writeRegister(REG_FIFO_RD_PTR, (rd_ptr + samples_to_read) % FIFO_DEPTH);

    // Clear interrupts / 清中断
    readRegister(REG_INTR_STATUS_1);
    readRegister(REG_INTR_STATUS_2);

    // Invoke callback if set / 如果设置，调用回调
    if (data_callback_ && !new_samples.empty()) {
        data_callback_(new_samples);
    }
}

// Write register / 写入寄存器
void Max30102Sensor::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    write(i2c_fd_, buf, 2);
}

// Read register / 读取寄存器
uint8_t Max30102Sensor::readRegister(uint8_t reg) {
    write(i2c_fd_, &reg, 1);
    uint8_t value;
    read(i2c_fd_, &value, 1);
    return value;
}
