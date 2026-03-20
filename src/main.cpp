#include "../include/HeartRateCalculator.h"
#include "../include/Sensor.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

double sampleRateToHz(SampleRate rate) {
    switch (rate) {
        case SAMPLERATE_50:   return 50.0;
        case SAMPLERATE_100:  return 100.0;
        case SAMPLERATE_200:  return 200.0;
        case SAMPLERATE_400:  return 400.0;
        case SAMPLERATE_800:  return 800.0;
        case SAMPLERATE_1000: return 1000.0;
        case SAMPLERATE_1600: return 1600.0;
        case SAMPLERATE_3200: return 3200.0;
        default:              return 100.0;
    }
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int interruptPin = DEFAULT_DRDY_GPIO;
    SampleAverage avg = SAMPLEAVG_4;
    SampleRate rate = SAMPLERATE_100;
    LedPulseWidth width = PULSEWIDTH_411;
    try {
        Max30102Sensor sensor(interruptPin, avg, rate, width);

        if (!sensor.initialize()) {
            std::cerr << "Sensor initialization failed." << std::endl;
            return 1;
        }

        HeartRateCalculator hr(sampleRateToHz(rate));

        sensor.setDataCallback([&hr](const std::vector<Sample>& samples) {
            std::cout << "callback called, samples=" << samples.size() << std::endl;//test
            hr.processSamples(samples);
        });

        sensor.start();

        std::cout << "Heart rate monitor started. Press Ctrl+C to stop." << std::endl;

        while (g_running) {
            bool finger = hr.fingerDetected();
            auto bpm = hr.getLatestBpm();

            if (!finger) {
                std::cout << "[INFO] No finger detected." << std::endl;
            } else if (bpm.has_value()) {
                std::cout << "[INFO] Heart Rate: " << *bpm << " BPM" << std::endl;
            } else {
                std::cout << "[INFO] Measuring..." << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        sensor.stop();
        std::cout << "Stopped." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}