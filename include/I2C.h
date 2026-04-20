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
    // Initialize the I2C bus and device address
    I2CDevice(int bus, int addr) {
        char filename[20];
        snprintf(filename, 19, "/dev/i2c-%d", bus);
        file = open(filename, O_RDWR);
        if (file < 0) {
            std::cerr << "Unable to open I2C bus" << std::endl;
            exit(1);
        }
        if (ioctl(file, I2C_SLAVE, addr) < 0) {
            std::cerr << "Unable to connect to the I2C device" << std::endl;
            exit(1);
        }
    }

    ~I2CDevice() {
        close(file);
    }

    // Alternative to Arduino's Wire.write()
    void writeRegister(uint8_t reg, uint8_t value) {
        uint8_t buffer[2] = {reg, value};
        write(file, buffer, 2);
    }

    // Alternative to Arduino's Wire.read()
    uint8_t readRegister(uint8_t reg) {
        write(file, &reg, 1); 
        uint8_t value;
        read(file, &value, 1); 
        return value;
    }
    
    // Continuously read multiple bytes (for reading red/light infrared data in the FIFO)
    void readRegisters(uint8_t reg, uint8_t* buffer, int len) {
        write(file, &reg, 1);
        read(file, buffer, len);
    }
};

#endif
