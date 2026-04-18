#include "../include/HeartRateCalculator.h"
#include "../include/sensor.h"
#include "../include/ZoneMusicPlayer.h"
#include "../include/SDL2AudioBackend.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

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

// 扫描某个 zone 文件夹下所有 .wav 文件
std::vector<std::string> getZoneTracks(int zone) {
    std::string path = "assets/music/zone" + std::to_string(zone);
    std::vector<std::string> tracks;
    if (!std::filesystem::exists(path)) {
        std::cerr << "[WARN] Music folder not found: " << path << std::endl;
        return tracks;
    }
    for (auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension() == ".wav") {
            tracks.push_back(entry.path().string());
        }
    }
    std::sort(tracks.begin(), tracks.end());
    std::cout << "[INFO] Zone " << zone << ": "
              << tracks.size() << " tracks loaded" << std::endl;
    return tracks;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int interruptPin = DEFAULT_DRDY_GPIO;
    SampleAverage avg = SAMPLEAVG_4;
    SampleRate rate = SAMPLERATE_100;
    LedPulseWidth width = PULSEWIDTH_411;

    try {
        // ── 传感器初始化 ──────────────────────────────────────
        Max30102Sensor sensor(interruptPin, avg, rate, width);

        if (!sensor.initialize()) {
            std::cerr << "[ERROR] Sensor initialization failed." << std::endl;
            return 1;
        }

        HeartRateCalculator hr(sampleRateToHz(rate));

        sensor.setDataCallback([&hr](const std::vector<Sample>& samples) {
            hr.processSamples(samples);
        });

        // ── ZoneMusicPlayer 初始化 ────────────────────────────
        auto backend = std::make_shared<SDL2AudioBackend>();
        ZoneMusicPlayer player(backend);

        player.loadZone(1, getZoneTracks(1));
        player.loadZone(2, getZoneTracks(2));
        player.loadZone(3, getZoneTracks(3));
        player.loadZone(4, getZoneTracks(4));
        player.loadZone(5, getZoneTracks(5));
        player.loadZone(6, getZoneTracks(6));

        // 区间切换时打印日志
        player.setTransitionCallback([](int zone) {
            const char* names[] = {
                "Zone1 (< 80 BPM)",
                "Zone2 (80-99 BPM)",
                "Zone3 (100-119 BPM)",
                "Zone4 (120-139 BPM)",
                "Zone5 (140-159 BPM)",
                "Zone6 (>= 160 BPM)"
            };
            std::cout << "[MUSIC] --> " << names[zone - 1] << std::endl;
        });

        sensor.start();
        std::cout << "VibePulse started. Press Ctrl+C to stop." << std::endl;

        // ── 主循环 ────────────────────────────────────────────
        while (g_running) {
            bool finger = hr.fingerDetected();
            auto bpm    = hr.getLatestBpm();

            if (!finger) {
                std::cout << "[INFO] No finger detected." << std::endl;
            } else if (bpm.has_value()) {
                int bpmInt = static_cast<int>(*bpm);
                std::cout << "[INFO] Heart Rate: " << bpmInt << " BPM"
                          << " | Zone: " << bpmToZone(bpmInt)
                          << std::endl;

                player.updateBPM(bpmInt);
            } else {
                std::cout << "[INFO] Measuring..." << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        sensor.stop();
        std::cout << "Stopped." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Fatal: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
