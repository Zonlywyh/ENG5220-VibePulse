// ============================================================
//  ZoneMusicPlayer.cpp — VibePulse ENG5220
// ============================================================

#include "../include/ZoneMusicPlayer.h"
#include <stdexcept>
#include <chrono>
#include <algorithm>

ZoneMusicPlayer::ZoneMusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend)
        throw std::invalid_argument("ZoneMusicPlayer: backend must not be null");
    m_monitor = std::thread(&ZoneMusicPlayer::monitorLoop, this);
}

ZoneMusicPlayer::~ZoneMusicPlayer() {
    m_monitorStop.store(true);
    if (m_monitor.joinable())
        m_monitor.join();

    stopWorker();

    for (auto& handles : m_zoneHandles)
        for (int h : handles)
            m_backend->freeTrack(h);
}

// ─────────────────────────────────────────────────────────────
//  Load
// ─────────────────────────────────────────────────────────────
bool ZoneMusicPlayer::loadZone(int zone, const std::vector<std::string>& paths) {
    if (zone < 1 || zone > ZONE_COUNT) return false;
    auto& handles = m_zoneHandles[zone - 1];
    for (const auto& p : paths) {
        int h = m_backend->loadTrack(p);
        if (h < 0) return false;
        handles.push_back(h);
        m_handlePaths[h] = p;
    }
    // Auto-start zone 1's first track (play once; monitor will auto-advance)
    if (zone == 1 && m_currentHandle < 0 && !handles.empty()) {
        m_currentHandle = handles[0];
        m_backend->play(m_currentHandle, 0);
        m_backend->setVolume(m_currentHandle, 128);
        m_volIn.store(128);
        std::lock_guard<std::mutex> lk(m_pathMutex);
        m_currentTrackPath = paths[0];
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
//  BPM → zone
// ─────────────────────────────────────────────────────────────
void ZoneMusicPlayer::updateBPM(int bpm) {
    int target = bpmToZone(bpm);
    if (target != m_currentZone.load())
        setZone(target);
}

// ─────────────────────────────────────────────────────────────
//  Track path accessors
// ─────────────────────────────────────────────────────────────
std::optional<std::string> ZoneMusicPlayer::currentTrackPath() const {
    std::lock_guard<std::mutex> lk(m_pathMutex);
    if (m_currentTrackPath.empty()) return std::nullopt;
    return m_currentTrackPath;
}

std::optional<std::string> ZoneMusicPlayer::targetTrackPath() const {
    return currentTrackPath();
}

// ─────────────────────────────────────────────────────────────
//  Pick random track (avoid repeating current)
// ─────────────────────────────────────────────────────────────
int ZoneMusicPlayer::pickRandomExcept(int zone, int excludeHandle) {
    if (zone < 1 || zone > ZONE_COUNT) return -1;
    auto& handles = m_zoneHandles[zone - 1];
    if (handles.empty()) return -1;
    if (handles.size() == 1) return handles[0];

    int picked = excludeHandle;
    int attempts = 0;
    while (picked == excludeHandle && attempts < 10) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(handles.size()) - 1);
        picked = handles[dist(m_rng)];
        ++attempts;
    }
    return picked;
}

// ─────────────────────────────────────────────────────────────
//  Background monitor — detects track end and auto-advances
// ─────────────────────────────────────────────────────────────
void ZoneMusicPlayer::monitorLoop() {
    while (!m_monitorStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (m_crossfading.load()) continue;

        int handle = m_currentHandle;
        if (handle < 0) continue;

        if (m_backend->isFinished(handle)) {
            int zone = m_currentZone.load();
            int nextHandle = pickRandomExcept(zone, handle);
            if (nextHandle < 0) continue;

            m_backend->halt(handle);
            m_currentHandle = nextHandle;
            m_backend->play(nextHandle, 0);
            m_backend->setVolume(nextHandle, 128);

            auto it = m_handlePaths.find(nextHandle);
            if (it != m_handlePaths.end()) {
                std::lock_guard<std::mutex> lk(m_pathMutex);
                m_currentTrackPath = it->second;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  Zone switch with crossfade
// ─────────────────────────────────────────────────────────────
void ZoneMusicPlayer::setZone(int zone) {
    zone = std::clamp(zone, 1, ZONE_COUNT);
    if (m_currentZone.load() == zone) return;
    if (!m_backend->isReady()) return;

    int hIn = pickRandomExcept(zone, m_currentHandle);
    if (hIn < 0) return;

    stopWorker();
    m_stopRequested.store(false);
    m_crossfading.store(true);

    int hOut = m_currentHandle;
    m_currentHandle = hIn;

    auto it = m_handlePaths.find(hIn);
    if (it != m_handlePaths.end()) {
        std::lock_guard<std::mutex> lk(m_pathMutex);
        m_currentTrackPath = it->second;
    }

    m_backend->play(hIn, 0);
    m_backend->setVolume(hIn, 0);

    m_worker = std::thread([this, hOut, hIn, zone]() {
        runCrossfade(hOut, hIn, zone);
    });
    m_worker.detach();
}

void ZoneMusicPlayer::stopWorker() {
    m_stopRequested.store(true);
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
    m_crossfading.store(false);
}

// ─────────────────────────────────────────────────────────────
//  Crossfade worker
// ─────────────────────────────────────────────────────────────
void ZoneMusicPlayer::runCrossfade(int hOut, int hIn, int next) {
    for (int step = 1; step <= CROSSFADE_STEPS; ++step) {
        if (m_stopRequested.load()) {
            m_crossfading.store(false);
            return;
        }
        int volIn  = static_cast<int>((128.0f * step) / CROSSFADE_STEPS);
        int volOut = 128 - volIn;

        if (hIn  >= 0) m_backend->setVolume(hIn,  volIn);
        if (hOut >= 0) m_backend->setVolume(hOut, volOut);

        m_volIn.store(volIn);
        m_volOut.store(volOut);

        {
            std::unique_lock<std::mutex> lk(m_cv_mutex);
            m_cv.wait_for(lk, std::chrono::milliseconds(CROSSFADE_STEP_MS),
                          [this] { return m_stopRequested.load(); });
        }
    }

    if (hIn  >= 0) m_backend->setVolume(hIn,  128);
    if (hOut >= 0) {
        m_backend->setVolume(hOut, 0);
        m_backend->halt(hOut);
    }

    m_volIn.store(128);
    m_volOut.store(0);
    m_currentZone.store(next);
    m_crossfading.store(false);

    if (m_onTransition) m_onTransition(next);
}

void ZoneMusicPlayer::setTransitionCallback(std::function<void(int)> cb) {
    m_onTransition = std::move(cb);
}
