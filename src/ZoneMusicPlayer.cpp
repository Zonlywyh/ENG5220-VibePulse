// ============================================================
//  ZoneMusicPlayer.cpp - VibePulse ENG5220
// ============================================================

#include "../include/ZoneMusicPlayer.h"
#include <stdexcept>

namespace {
constexpr auto kZoneRotateInterval = std::chrono::seconds(30);
}

ZoneMusicPlayer::ZoneMusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend)) {
    if (!m_backend) {
        throw std::invalid_argument("ZoneMusicPlayer: backend must not be null");
    }

    m_eventThread = std::thread(&ZoneMusicPlayer::eventLoop, this);
    m_backend->setTrackFinishedCallback([this](int handle) {
        enqueueEvent(EventType::TrackFinished, handle);
    });
}

ZoneMusicPlayer::~ZoneMusicPlayer() {
    m_backend->setTrackFinishedCallback({});
    enqueueEvent(EventType::Shutdown, 0);
    if (m_eventThread.joinable()) {
        m_eventThread.join();
    }
    stopWorker();

    for (auto& handles : m_zoneHandles) {
        for (int h : handles) {
            m_backend->freeTrack(h);
        }
    }
}

bool ZoneMusicPlayer::loadZone(int zone, const std::vector<std::string>& paths) {
    if (zone < 1 || zone > ZONE_COUNT) {
        return false;
    }

    auto& handles = m_zoneHandles[zone - 1];
    for (const auto& path : paths) {
        const int handle = m_backend->loadTrack(path);
        if (handle < 0) {
            return false;
        }
        handles.push_back(handle);
        m_handlePaths[handle] = path;
    }

    if (zone == 1 && m_currentHandle < 0 && !handles.empty()) {
        m_currentHandle = handles[0];
        const int loops = handles.size() == 1 ? -1 : 0;
        m_backend->play(m_currentHandle, loops);
        m_backend->setVolume(m_currentHandle, 128);
        m_volIn.store(128);
        refreshTrackDeadline(zone);
        std::lock_guard<std::mutex> lk(m_pathMutex);
        m_currentTrackPath = paths[0];
    }

    return true;
}

bool ZoneMusicPlayer::loadZoneTracks(const std::array<std::string, kZoneCount>& paths) {
    for (int z = 1; z <= kZoneCount; ++z) {
        const auto& path = paths[z - 1];
        if (path.empty()) {
            return false;
        }
        if (!loadZone(z, std::vector<std::string>{path})) {
            return false;
        }
    }
    return true;
}

void ZoneMusicPlayer::updateBPM(int bpm) {
    setZone(bpmToZone(bpm));
}

void ZoneMusicPlayer::setZone(int zone) {
    zone = std::clamp(zone, 1, ZONE_COUNT);
    const int previousTarget = m_targetZone.exchange(zone);
        if (previousTarget == zone && (m_crossfading.load() || m_currentZone.load() == zone)) {
        return;
    }
    enqueueEvent(EventType::ZoneChange, zone);
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
    if (m_currentTrackPath.empty()) {
        return std::nullopt;
    }
    return m_currentTrackPath;
}

std::optional<std::string> ZoneMusicPlayer::targetTrackPath() const {
    return currentTrackPath();
}

void ZoneMusicPlayer::enqueueEvent(EventType type, int value) {
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        if (m_eventStop) {
            return;
        }
        if (type == EventType::Shutdown) {
            m_eventStop = true;
            m_pendingEvents.clear();
        }
        m_pendingEvents.push_back(PendingEvent{type, value});
    }
    m_eventCv.notify_one();
}

void ZoneMusicPlayer::eventLoop() {
    while (true) {
        PendingEvent event{EventType::Shutdown, 0};
        bool timed_out = false;
        {
            std::unique_lock<std::mutex> lock(m_eventMutex);
            const auto deadline = m_trackDeadline;
            if (deadline == std::chrono::steady_clock::time_point::max()) {
                m_eventCv.wait(lock, [this] {
                    return !m_pendingEvents.empty();
                });
            } else {
                m_eventCv.wait_until(lock, deadline, [this] {
                    return !m_pendingEvents.empty();
                });
                timed_out = m_pendingEvents.empty() &&
                            std::chrono::steady_clock::now() >= m_trackDeadline;
            }
            if (!m_pendingEvents.empty()) {
                event = m_pendingEvents.front();
                m_pendingEvents.pop_front();
            }
        }

        if (timed_out) {
            performRotateWithinZone(m_currentZone.load());
            continue;
        }
        if (event.type == EventType::Shutdown) {
            break;
        }
        if (event.type == EventType::TrackFinished) {
            handleTrackFinished(event.value);
            continue;
        }
        if (event.type == EventType::RotateTrack) {
            performRotateWithinZone(event.value);
            continue;
        }
        if (event.type == EventType::ZoneChange) {
            performSetZone(event.value);
        }
    }
}

int ZoneMusicPlayer::pickRandomExcept(int zone, int excludeHandle) {
    if (zone < 1 || zone > ZONE_COUNT) {
        return -1;
    }

    auto& handles = m_zoneHandles[zone - 1];
    if (handles.empty()) {
        return -1;
    }
    if (handles.size() == 1) {
        return handles[0];
    }

    int picked = excludeHandle;
    int attempts = 0;
    while (picked == excludeHandle && attempts < 10) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(handles.size()) - 1);
        picked = handles[dist(m_rng)];
        ++attempts;
    }
    return picked;
}

void ZoneMusicPlayer::handleTrackFinished(int handle) {
    if (m_crossfading.load()) {
        return;
    }
    if (handle != m_currentHandle) {
        return;
    }

    const int zone = m_currentZone.load();
    const int nextHandle = pickRandomExcept(zone, handle);
    if (nextHandle < 0) {
        return;
    }

    m_backend->halt(handle);
    m_currentHandle = nextHandle;
    const int loops = m_zoneHandles[zone - 1].size() == 1 ? -1 : 0;
    m_backend->play(nextHandle, loops);
    m_backend->setVolume(nextHandle, 128);
    refreshTrackDeadline(zone);

    auto it = m_handlePaths.find(nextHandle);
    if (it != m_handlePaths.end()) {
        {
            std::lock_guard<std::mutex> lk(m_pathMutex);
            m_currentTrackPath = it->second;
        }
        if (m_onTransition) {
            m_onTransition(zone);
        }
    }
}

void ZoneMusicPlayer::performRotateWithinZone(int zone) {
    if (zone < 1 || zone > ZONE_COUNT) {
        return;
    }
    if (m_crossfading.load()) {
        refreshTrackDeadline(zone);
        return;
    }

    auto& handles = m_zoneHandles[zone - 1];
    if (handles.size() <= 1 || m_currentZone.load() != zone) {
        refreshTrackDeadline(zone);
        return;
    }

    const int nextHandle = pickRandomExcept(zone, m_currentHandle);
    if (nextHandle < 0 || nextHandle == m_currentHandle) {
        refreshTrackDeadline(zone);
        return;
    }

    const int previousHandle = m_currentHandle;
    m_currentHandle = nextHandle;
    m_backend->halt(previousHandle);
    m_backend->play(nextHandle, 0);
    m_backend->setVolume(nextHandle, 128);
    refreshTrackDeadline(zone);

    auto it = m_handlePaths.find(nextHandle);
    if (it != m_handlePaths.end()) {
        {
            std::lock_guard<std::mutex> lk(m_pathMutex);
            m_currentTrackPath = it->second;
        }
        if (m_onTransition) {
            m_onTransition(zone);
        }
    }
}

void ZoneMusicPlayer::performSetZone(int zone) {
    zone = std::clamp(zone, 1, ZONE_COUNT);
    m_targetZone.store(zone);
    if (m_currentZone.load() == zone) {
        return;
    }
    if (!m_backend->isReady()) {
        return;
    }

    const int hIn = pickRandomExcept(zone, m_currentHandle);
    if (hIn < 0) {
        return;
    }

    stopWorker();
    m_stopRequested.store(false);
    m_crossfading.store(true);

    const int hOut = m_currentHandle;
    m_currentHandle = hIn;

    auto it = m_handlePaths.find(hIn);
    if (it != m_handlePaths.end()) {
        std::lock_guard<std::mutex> lk(m_pathMutex);
        m_currentTrackPath = it->second;
    }

    const int loops = m_zoneHandles[zone - 1].size() == 1 ? -1 : 0;
    m_backend->play(hIn, loops);
    m_backend->setVolume(hIn, 0);
    refreshTrackDeadline(zone);

    m_worker = std::thread([this, hOut, hIn, zone]() {
        runCrossfade(hOut, hIn, zone);
    });
}

void ZoneMusicPlayer::stopWorker() {
    m_stopRequested.store(true);
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_crossfading.store(false);
}

void ZoneMusicPlayer::runCrossfade(int hOut, int hIn, int next) {
    for (int step = 1; step <= CROSSFADE_STEPS; ++step) {
        if (m_stopRequested.load()) {
            m_crossfading.store(false);
            return;
        }
        int volIn  = static_cast<int>((128.0f * step) / CROSSFADE_STEPS);
        int volOut = 128 - volIn;

        const int volIn = static_cast<int>((128.0f * step) / CROSSFADE_STEPS);
        const int volOut = 128 - volIn;

        if (hIn >= 0) {
            m_backend->setVolume(hIn, volIn);
        }
        if (hOut >= 0) {
            m_backend->setVolume(hOut, volOut);
        }

        m_volIn.store(volIn);
        m_volOut.store(volOut);

        std::unique_lock<std::mutex> lk(m_cv_mutex);
        m_cv.wait_for(lk, std::chrono::milliseconds(CROSSFADE_STEP_MS), [this] {
            return m_stopRequested.load();
        });
    }

    if (hIn >= 0) {
        m_backend->setVolume(hIn, 128);
    }
    if (hOut >= 0) {
        m_backend->setVolume(hOut, 0);
        m_backend->halt(hOut);
    }

    m_volIn.store(128);
    m_volOut.store(0);
    m_currentZone.store(next);
    m_targetZone.store(next);
    m_crossfading.store(false);

    if (m_onTransition) {
        m_onTransition(next);
    }
}

void ZoneMusicPlayer::setTransitionCallback(std::function<void(int)> cb) {
    m_onTransition = std::move(cb);
}

void ZoneMusicPlayer::refreshTrackDeadline(int zone) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    if (zone < 1 || zone > ZONE_COUNT || m_zoneHandles[zone - 1].size() <= 1) {
        m_trackDeadline = std::chrono::steady_clock::time_point::max();
    } else {
        m_trackDeadline = std::chrono::steady_clock::now() + kZoneRotateInterval;
    }
    m_eventCv.notify_one();
}
