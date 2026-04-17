#include "../include/HeartRateCalculator.h"
#include "../include/Sensor.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};

namespace {

double sampleRateToHz(SampleRate rate)
{
    switch (rate) {
    case SAMPLERATE_50:
        return 50.0;
    case SAMPLERATE_100:
        return 100.0;
    case SAMPLERATE_200:
        return 200.0;
    case SAMPLERATE_400:
        return 400.0;
    case SAMPLERATE_800:
        return 800.0;
    case SAMPLERATE_1000:
        return 1000.0;
    case SAMPLERATE_1600:
        return 1600.0;
    case SAMPLERATE_3200:
        return 3200.0;
    default:
        return 100.0;
    }
}

void signalHandler(int)
{
    g_running = false;
}

} // namespace

int main()
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    const int interruptPin = DEFAULT_DRDY_GPIO;
    const SampleAverage avg = SAMPLEAVG_4;
    const SampleRate rate = SAMPLERATE_50;
    const LedPulseWidth width = PULSEWIDTH_411;

    try {
        Max30102Sensor sensor(interruptPin, avg, rate, width);
        HeartRateCalculator calculator(sampleRateToHz(rate));

        if (!sensor.initialize()) {
            std::cerr << "Sensor initialization failed." << std::endl;
            return 1;
        }

        sensor.setDataCallback([&calculator](const std::vector<Sample>& samples) {
            calculator.processSamples(samples);
        });

        sensor.start();
        std::cout << "Heart rate monitor started. Press Ctrl+C to stop." << std::endl;

        std::string last_status;
        auto last_log_time = std::chrono::steady_clock::now();
        unsigned long long output_counter = 0;

        while (g_running) {
            const std::optional<double> bpm = calculator.getLatestBpm();
            const bool finger_detected = calculator.fingerDetected();

            std::string status_message;
            if (!calculator.hasSamples()) {
                status_message = "[INFO] Waiting for sensor data...";
            } else if (!finger_detected) {
                status_message = "[INFO] Place finger on sensor.";
            } else if (bpm.has_value()) {
                const int rounded_bpm = static_cast<int>(*bpm + 0.5);
                status_message = "[INFO] Heart Rate: " + std::to_string(rounded_bpm) + " BPM";
            } else {
                status_message = "[INFO] Measuring...";
            }

            const auto now = std::chrono::steady_clock::now();
            if (status_message != last_status ||
                now - last_log_time > std::chrono::milliseconds(800)) {
                ++output_counter;
                std::cout << "[" << output_counter << "] " << status_message << std::endl;
                last_status = status_message;
                last_log_time = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        sensor.stop();
        std::cout << "Stopped." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
