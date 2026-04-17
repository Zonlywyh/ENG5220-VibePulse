#include "../include/Sensor.h"
#include "../include/dsp.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <string>
#include <mutex>
#include <vector>
#include <optional>

/**
 * 注意：为了保证主线程读取 BPM 和回调线程写入 BPM 不冲突，
 * 建议在你的 HeartRateDSP 类内部对 getCurrentBPM() 和 detectHeartRate() 加锁。
 */

static std::atomic<bool> g_running{true};

void signalHandler(int)
{
    g_running = false;
}

int main()
{
    // 注册信号捕获，确保 Ctrl+C 能优雅退出
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 传感器配置参数
    int interruptPin = DEFAULT_DRDY_GPIO;
    SampleAverage avg = SAMPLEAVG_4;
    SampleRate rate = SAMPLERATE_50;
    LedPulseWidth width = PULSEWIDTH_411;

    try
    {
        // 1. 实例化 DSP 处理器
        // 确保 myDSP 的生命周期覆盖整个传感器运行周期
        HeartRateDSP myDSP;

        // 2. 实例化并初始化传感器
        Max30102Sensor sensor(interruptPin, avg, rate, width);

        if (!sensor.initialize())
        {
            std::cerr << "[ERROR] Sensor initialization failed. Check connection/I2C." << std::endl;
            return 1;
        }

        // 打印显示相关的变量
        std::string last_status = "";
        auto last_log_time = std::chrono::steady_clock::now();
        unsigned long long output_counter = 0;

        // 3. 设置数据回调 (这是传感器数据流入的地方)
        // 使用 [&] 捕获 myDSP，因为 myDSP 在本作用域内是安全的
        sensor.setDataCallback([&myDSP](const std::vector<Sample> &samples)
                               {
            for (const auto& s : samples) {
                // 手指检测阈值判断 (假设红光强度低于 30000 为脱离)
                if (s.red < 30000) { 
                    myDSP.reset(); 
                    continue; 
                }

                // 1. 滤波处理
                float filtered = myDSP.processSample(s.red);
                
                // 2. 获取当前毫秒级时间戳
                auto now = std::chrono::steady_clock::now();
                uint32_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()).count();
                
                // 3. 核心算法：检测心率
                myDSP.detectHeartRate(filtered, ts);
            } });

        // 4. 启动传感器采集线程
        sensor.start();
        std::cout << "--- Heart Rate Monitor System ---" << std::endl;
        std::cout << "Status: Running... Press Ctrl+C to stop." << std::endl;

        // 5. 主循环：负责结果输出与 UI 更新
        while (g_running)
        {
            // 从 DSP 获取最新的心率计算结果
            float bpm_val = myDSP.getCurrentBPM();

            std::string status_message;

            // 逻辑判断：是否有有效心率
            if (bpm_val > 0)
            {
                int rounded_bpm = static_cast<int>(bpm_val + 0.5);
                status_message = "Heart Rate: " + std::to_string(rounded_bpm) + " BPM";
            }
            else
            {
                status_message = "Status: Measuring... (Place your finger on sensor)";
            }

            // 频率限制：只有当状态改变，或者距离上次打印超过 800ms 时才刷新屏幕
            auto now = std::chrono::steady_clock::now();
            if (status_message != last_status || (now - last_log_time > std::chrono::milliseconds(800)))
            {
                ++output_counter;
                // 加上 \r 或特定格式可以实现原地刷新，这里使用标准换行
                std::cout << "[" << output_counter << "] " << status_message << std::endl;

                last_status = status_message;
                last_log_time = now;
            }

            // 降低主循环 CPU 占用
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 6. 资源清理
        std::cout << "\nStopping sensor..." << std::endl;
        sensor.stop();
        std::cout << "Exited cleanly." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}