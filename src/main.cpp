#include "../include/HeartRateCalculator.h"
#include "../include/Sensor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#ifdef VIBEPULSE_ENABLE_AUDIO
#include "../include/SDL2AudioBackend.h"
#include "../include/ZoneMusicPlayer.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#endif

namespace {

// Global stop flag shared across threads. The signal handler only flips this
// atomic flag so shutdown stays async-signal-safe.
std::atomic<bool> g_running{true};

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

enum class MonitorState {
    WaitingForSamples,
    NoFinger,
    Measuring,
    HeartRateReady
};

struct AudioSnapshot {
    bool available = false;
    std::optional<std::string> current_track;
    std::optional<std::string> target_track;
};

// Unified application state shared by the heart-rate pipeline,
// the optional audio system, and the console presenter.
//
// This snapshot is the hand-off point between asynchronous producers
// (sensor callback, heart-rate callbacks, audio transitions) and the
// background presenter thread that prints status to the console.
struct AppSnapshot {
    bool has_samples = false;
    bool finger_detected = false;
    std::optional<int> bpm;
    AudioSnapshot audio;
};

// Thread-safe store that publishes state changes to waiting consumers.
//
// Producers update the snapshot from callback context, then notify the
// condition variable. Consumers wait for version changes instead of polling,
// which keeps the application event-driven rather than sleep-based.
class AppStateStore {
public:
    void markSamplesReceived() {
        update([](AppSnapshot& snapshot) {
            snapshot.has_samples = true;
        });
    }

    void setFingerDetected(bool finger_detected) {
        update([finger_detected](AppSnapshot& snapshot) {
            snapshot.finger_detected = finger_detected;
            if (!finger_detected) {
                snapshot.bpm.reset();
            }
        });
    }

    void setBpm(int bpm) {
        update([bpm](AppSnapshot& snapshot) {
            snapshot.bpm = bpm;
        });
    }

    void setAudioSnapshot(const AudioSnapshot& audio_snapshot) {
        update([&audio_snapshot](AppSnapshot& snapshot) {
            snapshot.audio = audio_snapshot;
        });
    }

    AppSnapshot waitForUpdate(std::uint64_t last_seen_version,
                              std::chrono::steady_clock::time_point deadline,
                              bool& changed) {
        std::unique_lock<std::mutex> lock(mutex_);
        // Wake up either when a producer publishes a newer snapshot or when a
        // periodic refresh deadline is reached.
        cv_.wait_until(lock, deadline, [&] {
            return stop_requested_ || version_ != last_seen_version;
        });

        changed = version_ != last_seen_version;
        return snapshot_;
    }

    std::uint64_t version() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return version_;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        }
        cv_.notify_all();
    }

private:
    template <typename UpdateFn>
    void update(UpdateFn&& update_fn) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            update_fn(snapshot_);
            ++version_;
        }
        // Notify all waiters because multiple background components may be
        // blocked on the latest state becoming available.
        cv_.notify_all();
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    AppSnapshot snapshot_;
    std::uint64_t version_ = 0;
    bool stop_requested_ = false;
};

// Background console logger that turns state snapshots into the
// user-facing status lines printed in the terminal.
//
// Console I/O is intentionally moved off the sensor/algorithm callback path.
// That keeps the fast path short and prevents printing from blocking signal
// processing or heart-rate updates.
class ConsolePresenter {
public:
    explicit ConsolePresenter(AppStateStore& state_store)
        : state_store_(state_store),
          worker_(&ConsolePresenter::run, this) {
    }

    ~ConsolePresenter() {
        stop();
    }

    void stop() {
        if (!stop_requested_.exchange(true)) {
            state_store_.stop();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    static MonitorState determineState(const AppSnapshot& snapshot) {
        if (!snapshot.has_samples) {
            return MonitorState::WaitingForSamples;
        }
        if (!snapshot.finger_detected) {
            return MonitorState::NoFinger;
        }
        if (snapshot.bpm.has_value()) {
            return MonitorState::HeartRateReady;
        }
        return MonitorState::Measuring;
    }

    static std::string makeStatusMessage(const AppSnapshot& snapshot) {
        std::string status_message;
        switch (determineState(snapshot)) {
            case MonitorState::WaitingForSamples:
                status_message = "[INFO] Waiting for sensor data...";
                break;
            case MonitorState::NoFinger:
                status_message = "[INFO] No finger detected. Place your fingertip steadily on the sensor.";
                break;
            case MonitorState::HeartRateReady:
                status_message = "[INFO] Heart Rate: " + std::to_string(*snapshot.bpm) + " BPM";
                break;
            case MonitorState::Measuring:
            default:
                status_message = "[INFO] Measuring...";
                break;
        }

        if (snapshot.audio.current_track.has_value()) {
            status_message += " | Music: " + *snapshot.audio.current_track;
        }
        if (snapshot.audio.target_track.has_value() &&
            snapshot.audio.target_track != snapshot.audio.current_track) {
            status_message += " -> " + *snapshot.audio.target_track;
        }

        return status_message;
    }

    // Timing diagnostics are only emitted on actual state edges,
    // not on periodic refreshes of the same state.
    void logStateTransitionTiming(MonitorState state, std::chrono::steady_clock::time_point now) {
        if (state == MonitorState::NoFinger &&
            last_state_ == MonitorState::Measuring &&
            last_measuring_time_.has_value()) {
            const auto measuring_to_no_finger_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_measuring_time_).count();
            std::cout << "    [TIMING] Measuring -> No finger: "
                      << measuring_to_no_finger_ms << " ms" << std::endl;
        } else if (state == MonitorState::Measuring &&
                   last_state_ == MonitorState::NoFinger &&
                   last_no_finger_time_.has_value()) {
            const auto no_finger_to_measuring_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_no_finger_time_).count();
            std::cout << "    [TIMING] No finger -> Measuring: "
                      << no_finger_to_measuring_ms << " ms" << std::endl;
        }

        if (state == MonitorState::NoFinger) {
            last_no_finger_time_ = now;
        } else if (state == MonitorState::Measuring) {
            last_measuring_time_ = now;
        }
    }

    void publish(const AppSnapshot& snapshot, bool periodic_refresh) {
        const auto now = std::chrono::steady_clock::now();
        const MonitorState state = determineState(snapshot);
        const std::string status_message = makeStatusMessage(snapshot);
        const bool state_changed = !last_state_.has_value() || state != *last_state_;
        const bool message_changed = status_message != last_status_;
        const bool bpm_changed = snapshot.bpm != last_bpm_;

        if (!state_changed && !message_changed && !bpm_changed && !periodic_refresh) {
            return;
        }

        ++output_counter_;
        const auto since_last_output_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time_).count();
        std::cout << "[" << output_counter_ << "][+" << since_last_output_ms << "ms] "
                  << status_message << std::endl;

        if (state_changed) {
            logStateTransitionTiming(state, now);
        }

        last_status_ = status_message;
        last_bpm_ = snapshot.bpm;
        last_state_ = state;
        last_output_time_ = now;
        last_publish_time_ = now;
    }

    // Wait for either a real state update or a periodic refresh deadline,
    // then publish the latest snapshot to the console.
    //
    // This worker thread is the consumer side of the event-driven pipeline:
    // callbacks publish state, this thread wakes on the condition variable,
    // and only then performs the slower console formatting and I/O.
    void run() {
        std::uint64_t seen_version = state_store_.version();

        while (!stop_requested_.load()) {
            const auto refresh_interval =
                (last_state_.has_value() && *last_state_ == MonitorState::HeartRateReady)
                    ? std::chrono::milliseconds(800)
                    : std::chrono::seconds(3);
            const auto deadline = last_publish_time_ + refresh_interval;

            bool changed = false;
            const AppSnapshot snapshot = state_store_.waitForUpdate(seen_version, deadline, changed);
            if (stop_requested_.load()) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool periodic_refresh = now >= deadline;
            seen_version = state_store_.version();
            publish(snapshot, periodic_refresh && !changed);
        }
    }

    AppStateStore& state_store_;
    std::atomic<bool> stop_requested_{false};
    std::thread worker_;
    std::optional<MonitorState> last_state_;
    std::string last_status_;
    std::optional<int> last_bpm_;
    std::chrono::steady_clock::time_point last_publish_time_ =
        std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::chrono::steady_clock::time_point last_output_time_ = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> last_no_finger_time_;
    std::optional<std::chrono::steady_clock::time_point> last_measuring_time_;
    unsigned long long output_counter_ = 0;
};

#ifdef VIBEPULSE_ENABLE_AUDIO
// Owns optional audio setup and feeds the resulting playback metadata
// back into the shared application state.
class AudioService {
public:
    explicit AudioService(AppStateStore& state_store)
        : state_store_(state_store) {
    }

    void initialize(int argc, char** argv) {
        std::cout << "[AUDIO] entered audio init block" << std::endl;

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

            if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
                return std::nullopt;
            }

            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                const auto ext = entry.path().extension().string();
                if (ext == ".wav" || ext == ".WAV") {
                    wavs.push_back(entry.path());
                }
            }

            if (wavs.empty()) {
                return std::nullopt;
            }

            std::sort(wavs.begin(), wavs.end());
            return wavs.front().string();
        };

        try {
            std::cout << "[AUDIO] creating backend" << std::endl;
            auto backend = std::make_shared<SDL2AudioBackend>();
            std::cout << "[AUDIO] creating zone player" << std::endl;
            zone_player_ = std::make_unique<ZoneMusicPlayer>(backend);

            std::array<std::string, ZoneMusicPlayer::kZoneCount> paths{};
            bool ok = true;
            for (int z = 1; z <= ZoneMusicPlayer::kZoneCount; ++z) {
                auto path = pick_zone_wav(z);
                if (!path.has_value()) {
                    std::cerr << "[AUDIO] Missing .wav in: "
                              << (music_root + "/zone" + std::to_string(z)) << std::endl;
                    ok = false;
                    break;
                }
                paths[z - 1] = *path;
            }

            std::cout << "[AUDIO] loading zone tracks" << std::endl;
            if (ok && !zone_player_->loadZoneTracks(paths)) {
                std::cerr << "[AUDIO] Failed to load zone tracks. Check WAV format/paths." << std::endl;
                zone_player_.reset();
            } else if (zone_player_) {
                std::cout << "[AUDIO] Zone player ready. Root=" << music_root << std::endl;
                for (int z = 1; z <= ZoneMusicPlayer::kZoneCount; ++z) {
                    std::cout << "[AUDIO] zone" << z << " -> " << paths[z - 1] << std::endl;
                }
                std::cout << "[AUDIO] initial -> zone1 -> " << paths[0] << std::endl;

                // Audio crossfades are asynchronous inside ZoneMusicPlayer.
                // When a transition completes, the callback republishes the
                // current/target track names into the shared application state.
                zone_player_->setTransitionCallback([this](int) {
                    publishAudioSnapshot();
                });
                publishAudioSnapshot();
            }
        } catch (const std::exception& e) {
            std::cerr << "[AUDIO] Disabled: " << e.what() << std::endl;
            zone_player_.reset();
            publishAudioSnapshot();
        }
    }

    void updateBpm(int bpm) {
        if (!zone_player_) {
            return;
        }
        // BPM updates arrive from the heart-rate callback path. AudioService
        // forwards them to the player, which decides whether a zone switch
        // should start based on its own debounce/crossfade rules.
        zone_player_->updateBPM(bpm);
        publishAudioSnapshot();
    }

private:
    void publishAudioSnapshot() {
        AudioSnapshot snapshot;
        snapshot.available = zone_player_ != nullptr;

        if (zone_player_) {
            snapshot.current_track = trackStem(zone_player_->currentTrackPath());
            snapshot.target_track = trackStem(zone_player_->targetTrackPath());
        }

        state_store_.setAudioSnapshot(snapshot);
    }

    static std::optional<std::string> trackStem(const std::optional<std::string>& path) {
        if (!path.has_value()) {
            return std::nullopt;
        }
        namespace fs = std::filesystem;
        const std::string stem = fs::path(*path).stem().string();
        if (stem.empty()) {
            return std::nullopt;
        }
        return stem;
    }

    AppStateStore& state_store_;
    std::unique_ptr<ZoneMusicPlayer> zone_player_;
};
#else
class AudioService {
public:
    explicit AudioService(AppStateStore&) {}
    void initialize(int, char**) {}
    void updateBpm(int) {}
};
#endif

// Top-level coordinator that wires sensor input, heart-rate processing,
// optional audio, and console presentation into one application.
class MonitorApplication {
public:
    MonitorApplication(int argc, char** argv)
        : argc_(argc),
          argv_(argv),
          sensor_(DEFAULT_DRDY_GPIO, SAMPLEAVG_4, SAMPLERATE_50, PULSEWIDTH_411),
          heart_rate_(sampleRateToHz(SAMPLERATE_50)),
          presenter_(state_store_),
          audio_service_(state_store_) {
    }

    int run() {
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        try {
            if (!sensor_.initialize()) {
                std::cerr << "Sensor initialization failed." << std::endl;
                return 1;
            }

            audio_service_.initialize(argc_, argv_);
            wireCallbacks();

            sensor_.start();
            std::cout << "Heart rate monitor started. Press Ctrl+C to stop." << std::endl;

            // The sensor itself is event-driven internally: its worker blocks on
            // GPIO/I/O readiness and invokes callbacks when new FIFO samples are
            // available. main() therefore does not poll for samples here; it
            // simply waits for a shutdown signal.
            while (g_running.load()) {
                pause();
            }

            sensor_.stop();
            presenter_.stop();
            std::cout << "Stopped." << std::endl;
        } catch (const std::exception& e) {
            presenter_.stop();
            std::cerr << "Fatal error: " << e.what() << std::endl;
            return 1;
        }

        return 0;
    }

private:
    // Connect sensor callbacks to the shared state store and to the
    // downstream services that consume heart-rate updates.
    //
    // The flow is:
    // 1. Sensor callback fires when the lower-level sensor worker has new data.
    // 2. IR samples are passed into HeartRateCalculator.
    // 3. HeartRateCalculator emits finger/BPM callbacks as state changes occur.
    // 4. Those callbacks update the shared snapshot and optionally drive audio.
    void wireCallbacks() {
        sensor_.setDataCallback([this](const std::vector<Sample>& samples) {
            state_store_.markSamplesReceived();

            // The sensor callback still keeps its work small: convert the batch
            // to IR values and hand it straight to the heart-rate estimator.
            std::vector<float> ir_samples;
            ir_samples.reserve(samples.size());
            for (const Sample& sample : samples) {
                ir_samples.push_back(sample.ir);
            }
            heart_rate_.processIrSamples(ir_samples);
        });

        // Finger detection changes are published as events into the shared
        // state store. The presenter thread consumes them and updates output.
        heart_rate_.setFingerStateCallback([this](bool finger_present) {
            state_store_.setFingerDetected(finger_present);
        });

        // BPM callbacks also feed the audio service. This keeps the music logic
        // driven by heart-rate events instead of a timer-based polling loop.
        heart_rate_.setBpmCallback([this](double bpm_value) {
            const int rounded_bpm = static_cast<int>(bpm_value + 0.5);
            state_store_.setBpm(rounded_bpm);
            audio_service_.updateBpm(rounded_bpm);
        });
    }

    int argc_;
    char** argv_;
    AppStateStore state_store_;
    Max30102Sensor sensor_;
    HeartRateCalculator heart_rate_;
    ConsolePresenter presenter_;
    AudioService audio_service_;
};

} // namespace

int main(int argc, char** argv) {
    MonitorApplication app(argc, argv);
    return app.run();
}
