#ifndef I2C_H
#define I2C_H

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

class I2CDevice {
private:
    int file;
public:
    // 初始化 I2C 总线和设备地址
    I2CDevice(int bus, int addr) {
        char filename[20];
        snprintf(filename, 19, "/dev/i2c-%d", bus);
        file = open(filename, O_RDWR);
        if (file < 0) {
            std::cerr << "无法打开 I2C 总线" << std::endl;
            exit(1);
        }
        if (ioctl(file, I2C_SLAVE, addr) < 0) {
            std::cerr << "无法连接到 I2C 设备" << std::endl;
            exit(1);
        }
    }

    ~I2CDevice() {
        close(file);
    }

    // 替代 Arduino 的 Wire.write()
    void writeRegister(uint8_t reg, uint8_t value) {
        uint8_t buffer[2] = {reg, value};
        write(file, buffer, 2);
    }

    // 替代 Arduino 的 Wire.read()
    uint8_t readRegister(uint8_t reg) {
        write(file, &reg, 1); // 先告诉设备你要读哪个寄存器
        uint8_t value;
        read(file, &value, 1); // 然后读取数据
        return value;
    }
    
    // 连续读取多个字节（用于读取 FIFO 里的红光/红外光数据）
    void readRegisters(uint8_t reg, uint8_t* buffer, int len) {
        write(file, &reg, 1);
        read(file, buffer, len);
    }
};

#endif