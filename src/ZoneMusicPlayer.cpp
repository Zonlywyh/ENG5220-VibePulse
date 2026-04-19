// ============================================================
//  ZoneMusicPlayer.cpp — VibePulse ENG5220
// ============================================================

#include "../include/ZoneMusicPlayer.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

// Uses steady_clock (monotonic) to avoid jumps from NTP/DST adjustments.
static long long steadyNowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Initialises handles to -1 (not loaded) and seeds debounce timestamps to now.
ZoneMusicPlayer::ZoneMusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend) {
        throw std::invalid_argument("ZoneMusicPlayer: backend must not be null");
    }
    m_handles.fill(-1);
    const long long now = steadyNowMs();
    m_desiredSinceMs.store(now);
    m_lastSwitchMs.store(now);
}

ZoneMusicPlayer::~ZoneMusicPlayer() {
    stopWorker();
    freeTracks();
}

// Sets stop flag and joins the worker thread to prevent data races on track state.
void ZoneMusicPlayer::stopWorker() {
    m_stopRequested.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_crossfading.store(false);
}

// Frees all backend track handles and resets them to -1.
void ZoneMusicPlayer::freeTracks() {
    for (int& h : m_handles) {
        if (h >= 0) {
            m_backend->freeTrack(h);
            h = -1;
        }
    }
    m_pathsLoaded.store(false);
}

bool ZoneMusicPlayer::loadZoneTracks(const std::array<std::string, kZoneCount>& zonePaths) {
    stopWorker();
    freeTracks();

    // Load all tracks first.
    for (int i = 0; i < kZoneCount; ++i) {
        m_zonePaths[i] = zonePaths[i];
        m_handles[i] = m_backend->loadTrack(zonePaths[i]);
        if (m_handles[i] < 0) {
            // Best-effort cleanup if any load fails.
            freeTracks();
            return false;
        }
    }
    m_pathsLoaded.store(true);

    // Start all tracks looping, but only zone1 audible initially.
    // All tracks play silently from t=0 to stay time-aligned for glitch-free crossfades.
    for (int i = 0; i < kZoneCount; ++i) {
        m_backend->play(m_handles[i], -1);
        m_backend->setVolume(m_handles[i], (i == 0) ? 128 : 0);
    }
    m_currentZone.store(1);
    m_targetZone.store(1);
    return true;
}

std::optional<std::string> ZoneMusicPlayer::currentTrackPath() const {
    if (!m_pathsLoaded.load()) return std::nullopt;
    const int zone = m_currentZone.load();
    if (zone < 1 || zone > kZoneCount) return std::nullopt;
    return m_zonePaths[zone - 1];
}

std::optional<std::string> ZoneMusicPlayer::targetTrackPath() const {
    if (!m_pathsLoaded.load()) return std::nullopt;
    const int zone = m_targetZone.load();
    if (zone < 1 || zone > kZoneCount) return std::nullopt;
    return m_zonePaths[zone - 1];
}

int ZoneMusicPlayer::bpmToZone(int bpm) const {
    // Simple 6-zone mapping. Adjust as needed for your project definition.
    // zone1: <= 60
    // zone2: 61..70
    // zone3: 71..80
    // zone4: 81..90
    // zone5: 91..100
    // zone6: >= 101
    if (bpm <= 76) return 1;
    if (bpm <= 90) return 2;
    if (bpm <= 110) return 3;
    if (bpm <= 140) return 4;
    if (bpm <= 150) return 5;
    return 6;
}

// Two-stage filter: zone must stay stable for kZoneStableMs, then respect kMinSwitchIntervalMs cooldown.
void ZoneMusicPlayer::updateBPM(int bpm) {
    const int desired = bpmToZone(bpm);
    const long long now = steadyNowMs();

    // Track how long the desired zone has been stable.
    const int lastDesired = m_lastDesiredZone.load();
    if (desired != lastDesired) {
        m_lastDesiredZone.store(desired);
        m_desiredSinceMs.store(now);
        return;
    }

    const int current = m_currentZone.load();
    if (desired == current) return;

    // Debounce + cooldown to avoid thrashing on noisy BPM boundaries.
    if (now - m_desiredSinceMs.load() < kZoneStableMs) return;
    if (now - m_lastSwitchMs.load() < kMinSwitchIntervalMs) return;

    setZone(desired);
}

// Cancels any running crossfade, then spawns a new worker thread for the transition.
void ZoneMusicPlayer::setZone(int zone) {
    if (zone < 1) zone = 1;
    if (zone > kZoneCount) zone = kZoneCount;

    const int current = m_currentZone.load();
    if (current == zone) return;
    if (!m_backend->isReady()) return;

    m_targetZone.store(zone);
    m_lastSwitchMs.store(steadyNowMs());

    // Interrupt any in-flight crossfade.
    m_stopRequested.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_stopRequested.store(false);
    m_crossfading.store(true);

    m_worker = std::thread([this, current, zone]() {
        runCrossfade(current, zone);
    });
}

// Linear crossfade: volIn ramps 0→128, volOut ramps 128→0 over CROSSFADE_STEPS ticks.
// Polls m_stopRequested each step for cooperative cancellation.
void ZoneMusicPlayer::runCrossfade(int fromZone, int toZone) {
    const int fromIdx = std::clamp(fromZone, 1, kZoneCount) - 1;
    const int toIdx   = std::clamp(toZone,   1, kZoneCount) - 1;

    const int hOut = m_handles[fromIdx];
    const int hIn  = m_handles[toIdx];
    if (hOut < 0 || hIn < 0) {
        m_crossfading.store(false);
        return;
    }

    for (int step = 1; step <= CROSSFADE_STEPS; ++step) {
        if (m_stopRequested.load()) {
            m_crossfading.store(false);
            return;
        }

        const int volIn  = static_cast<int>((128.0f * step) / CROSSFADE_STEPS);
        const int volOut = 128 - volIn;

        m_backend->setVolume(hIn, volIn);
        m_backend->setVolume(hOut, volOut);

        std::this_thread::sleep_for(std::chrono::milliseconds(CROSSFADE_STEP_MS));
    }

    // Snap to exact values to eliminate floating-point rounding drift.
    m_backend->setVolume(hIn, 128);
    m_backend->setVolume(hOut, 0);

    m_currentZone.store(toZone);
    m_targetZone.store(toZone);
    m_crossfading.store(false);

    if (m_onTransition) {
        m_onTransition(toZone);
    }
}

void ZoneMusicPlayer::setTransitionCallback(std::function<void(int)> cb) {
    m_onTransition = std::move(cb);
}
