#include "../include/HeartRateCalculator.h"
#include "../include/Sensor.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <string>
#include <mutex>
#include <optional>

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
