#include <iostream>
#include "I2C.h"

// MAX30102 的 I2C 地址通常是 0x57
#define MAX30102_I2C_ADDR 0x57

// MAX30102 的一些关键寄存器地址
#define REG_PART_ID 0xFF
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A

int main() {
    std::cout << "正在初始化 MAX30102..." << std::endl;

    // 实例化 I2C 设备：树莓派默认使用 I2C bus 1
    I2CDevice sensor(1, MAX30102_I2C_ADDR);

    // 1. 测试通信：读取芯片 ID
    uint8_t part_id = sensor.readRegister(REG_PART_ID);
    std::cout << "读取到的芯片 Part ID: 0x" << std::hex << (int)part_id << std::endl;

    if (part_id != 0x15) {
        std::cerr << "通信失败！这不是 MAX30102 芯片，或者接线有问题。" << std::endl;
        return 1;
    }
    std::cout << "通信成功！" << std::endl;

    // 2. 模拟 Arduino 的 setup() 初始化配置
    // 重置芯片
    sensor.writeRegister(REG_MODE_CONFIG, 0x40);
    usleep(10000); // 等待 10ms (类似 Arduino 的 delay(10))
    
    // 设置为 SpO2 模式 (红光和红外光都开启)
    sensor.writeRegister(REG_MODE_CONFIG, 0x03);
    
    // 设置 SpO2 采样率和脉冲宽度
    sensor.writeRegister(REG_SPO2_CONFIG, 0x27);

    std::cout << "芯片初始化配置完成！" << std::endl;

    return 0;
}