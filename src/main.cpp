#include "../include/HeartRateCalculator.h"
#include "../include/sensor.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <string>
#include <mutex>
#include <optional>

#ifdef VIBEPULSE_ENABLE_AUDIO
#include "../include/SDL2AudioBackend.h"
#include "../include/ZoneMusicPlayer.h"
#include <filesystem>
#include <array>
#include <vector>
#endif

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

int main(int argc, char** argv) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int interruptPin = DEFAULT_DRDY_GPIO;
    SampleAverage avg = SAMPLEAVG_4;
    SampleRate rate = SAMPLERATE_50;
    LedPulseWidth width = PULSEWIDTH_411;
    try {
        Max30102Sensor sensor(interruptPin, avg, rate, width);

        if (!sensor.initialize()) {
            std::cerr << "Sensor initialization failed." << std::endl;
            return 1;
        }

        HeartRateCalculator hr(sampleRateToHz(rate));
        std::mutex bpm_event_mutex;
        std::mutex finger_event_mutex;
        std::optional<int> latest_callback_bpm;
        std::optional<bool> latest_finger_state;

#ifdef VIBEPULSE_ENABLE_AUDIO
        // Optional audio bring-up:
        // - Expects `assets/music/zone1..zone6/` each containing at least one `.wav`.
        // - Override root via: `--music-root <path>`.
        std::string music_root = "assets/music";
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--music-root") {
                music_root = argv[i + 1];
            }
        }

        auto pick_zone_wav = [&](int zone) -> std::optional<std::string> {
            namespace fs = std::filesystem;
            const fs::path dir = fs::path(music_root) / ("zone" + std::to_string(zone));
            std::vector<fs::path> wavs;
            std::error_code ec;
            if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return std::nullopt;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                const auto ext = entry.path().extension().string();
                if (ext == ".wav" || ext == ".WAV") {
                    wavs.push_back(entry.path());
                }
            }
            if (wavs.empty()) return std::nullopt;
            std::sort(wavs.begin(), wavs.end());
            return wavs.front().string();
        };

        std::unique_ptr<ZoneMusicPlayer> zone_player;
        try {
            auto backend = std::make_shared<SDL2AudioBackend>();
            zone_player = std::make_unique<ZoneMusicPlayer>(backend);

            std::array<std::string, ZoneMusicPlayer::kZoneCount> paths{};
            bool ok = true;
            for (int z = 1; z <= ZoneMusicPlayer::kZoneCount; ++z) {
                auto p = pick_zone_wav(z);
                if (!p.has_value()) {
                    std::cerr << "[AUDIO] Missing .wav in: " << (music_root + "/zone" + std::to_string(z))
                              << std::endl;
                    ok = false;
                    break;
                }
                paths[z - 1] = *p;
            }

            if (ok && !zone_player->loadZoneTracks(paths)) {
                std::cerr << "[AUDIO] Failed to load zone tracks. Check WAV format/paths." << std::endl;
                zone_player.reset();
            } else if (zone_player) {
                std::cout << "[AUDIO] Zone player ready. Root=" << music_root << std::endl;
                for (int z = 1; z <= ZoneMusicPlayer::kZoneCount; ++z) {
                    std::cout << "[AUDIO] zone" << z << " -> " << paths[z - 1] << std::endl;
                }
                std::cout << "[AUDIO] initial -> zone1 -> " << paths[0] << std::endl;

                // Print which zone/track became active after each crossfade completes.
                zone_player->setTransitionCallback([paths](int zone) {
                    if (zone < 1 || zone > static_cast<int>(paths.size())) return;
                    std::cout << "[AUDIO] now -> zone" << zone << " -> " << paths[zone - 1] << std::endl;
                });
            }
        } catch (const std::exception& e) {
            std::cerr << "[AUDIO] Disabled: " << e.what() << std::endl;
            zone_player.reset();
        }
#endif

        sensor.setDataCallback([&hr](const std::vector<Sample>& samples) {
            hr.processSamples(samples);
        });

        hr.setBpmCallback([&](double bpm_value) {
            std::lock_guard<std::mutex> lock(bpm_event_mutex);
            latest_callback_bpm = static_cast<int>(bpm_value + 0.5);
        });

        hr.setFingerStateCallback([&](bool finger_present) {
            std::lock_guard<std::mutex> lock(finger_event_mutex);
            latest_finger_state = finger_present;
        });

        sensor.start();

        std::cout << "Heart rate monitor started. Press Ctrl+C to stop." << std::endl;
        std::string last_status;
        std::optional<bool> last_displayed_finger_state;
        std::optional<int> last_displayed_bpm;
        auto last_log_time = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto last_output_time = std::chrono::steady_clock::now();
        std::optional<std::chrono::steady_clock::time_point> last_no_finger_time;
        std::optional<std::chrono::steady_clock::time_point> last_measuring_time;
        unsigned long long output_counter = 0;

        while (g_running) {
            bool has_samples = hr.hasSamples();
            bool finger = hr.fingerDetected();
            auto bpm = hr.getLatestBpm();
            std::string status_message;
            std::optional<int> rounded_bpm;

            {
                std::lock_guard<std::mutex> lock(bpm_event_mutex);
                if (latest_callback_bpm.has_value()) {
                    rounded_bpm = latest_callback_bpm;
                }
            }
            {
                std::lock_guard<std::mutex> lock(finger_event_mutex);
                if (latest_finger_state.has_value()) {
                    finger = *latest_finger_state;
                }
            }

            if (!has_samples) {
                status_message = "[INFO] Waiting for sensor data...";
            } else if (!finger) {
                status_message = "[INFO] No finger detected. Place your fingertip steadily on the sensor.";
            } else if (bpm.has_value()) {
                if (!rounded_bpm.has_value()) {
                    rounded_bpm = static_cast<int>(*bpm + 0.5);
                }
                status_message = "[INFO] Heart Rate: " + std::to_string(*rounded_bpm) + " BPM";
            } else {
                status_message = "[INFO] Measuring...";
            }

#ifdef VIBEPULSE_ENABLE_AUDIO
            if (zone_player && finger && rounded_bpm.has_value()) {
                zone_player->updateBPM(*rounded_bpm);
            }
#endif

            auto now = std::chrono::steady_clock::now();
            bool status_changed = (status_message != last_status);
            bool bpm_changed = (rounded_bpm.has_value() && rounded_bpm != last_displayed_bpm);
            bool finger_changed = (finger != last_displayed_finger_state);
            bool periodic_refresh =
                (now - last_log_time >= (bpm.has_value() ? std::chrono::milliseconds(700)
                                                         : std::chrono::seconds(3)));

            if (status_changed || bpm_changed || finger_changed || periodic_refresh) {
                ++output_counter;
                auto since_last_output_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time).count();
                std::cout << "[" << output_counter << "][+" << since_last_output_ms << "ms] "
                          << status_message << std::endl;

                if (status_message == "[INFO] No finger detected. Place your fingertip steadily on the sensor.") {
                    if (last_measuring_time.has_value()) {
                        auto measuring_to_no_finger_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_measuring_time).count();
                        std::cout << "    [TIMING] Measuring -> No finger: "
                                  << measuring_to_no_finger_ms << " ms" << std::endl;
                    }
                    last_no_finger_time = now;
                } else if (status_message == "[INFO] Measuring...") {
                    if (last_no_finger_time.has_value()) {
                        auto no_finger_to_measuring_ms =
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_no_finger_time).count();
                        std::cout << "    [TIMING] No finger -> Measuring: "
                                  << no_finger_to_measuring_ms << " ms" << std::endl;
                    }
                    last_measuring_time = now;
                }

                last_status = status_message;
                last_displayed_finger_state = finger;
                last_displayed_bpm = rounded_bpm;
                last_log_time = now;
                last_output_time = now;
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
