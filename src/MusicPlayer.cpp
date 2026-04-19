#include "MusicPlayer.h"

#include <chrono>
#include <stdexcept>
#include <thread>

MusicPlayer::MusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend) {
        throw std::invalid_argument("MusicPlayer backend must not be null");
    }
}

MusicPlayer::~MusicPlayer()
{
    m_stopRequested.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_backend) {
        if (m_handleCalm >= 0) {
            m_backend->halt(m_handleCalm);
            m_backend->freeTrack(m_handleCalm);
            m_handleCalm = -1;
        }
        if (m_handleActive >= 0) {
            m_backend->halt(m_handleActive);
            m_backend->freeTrack(m_handleActive);
            m_handleActive = -1;
        }
    }
}

bool MusicPlayer::loadTracks(const std::string& calmPath, const std::string& activePath)
{
    if (!m_backend || !m_backend->isReady()) {
        return false;
    }

    if (m_handleCalm >= 0) {
        m_backend->freeTrack(m_handleCalm);
        m_handleCalm = -1;
    }
    if (m_handleActive >= 0) {
        m_backend->freeTrack(m_handleActive);
        m_handleActive = -1;
    }

    const int calm = m_backend->loadTrack(calmPath);
    if (calm < 0) {
        return false;
    }
    const int active = m_backend->loadTrack(activePath);
    if (active < 0) {
        m_backend->freeTrack(calm);
        return false;
    }

    m_handleCalm = calm;
    m_handleActive = active;

    m_backend->play(m_handleCalm, -1);
    m_backend->play(m_handleActive, -1);

    m_currentMode.store(MusicMode::CALM);
    m_volCalm.store(128);
    m_volActive.store(0);
    m_backend->setVolume(m_handleCalm, 128);
    m_backend->setVolume(m_handleActive, 0);

    return true;
}

void MusicPlayer::setTransitionCallback(std::function<void(MusicMode)> cb)
{
    m_onTransition = std::move(cb);
}

void MusicPlayer::updateBPM(int bpm)
{
    if (!m_backend || !m_backend->isReady()) {
        return;
    }
    if (m_handleCalm < 0 || m_handleActive < 0) {
        return;
    }

    const MusicMode mode = m_currentMode.load();
    if (mode == MusicMode::CALM && bpm >= BPM_ACTIVE_THRESHOLD) {
        crossfade(MusicMode::ACTIVE);
    } else if (mode == MusicMode::ACTIVE && bpm <= BPM_CALM_THRESHOLD) {
        crossfade(MusicMode::CALM);
    }
}

void MusicPlayer::crossfade(MusicMode nextMode)
{
    if (!m_backend || !m_backend->isReady()) {
        return;
    }

    const MusicMode from = m_currentMode.load();
    if (from == nextMode) {
        return;
    }

    m_stopRequested.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_stopRequested.store(false);

    m_crossfading.store(true);
    m_worker = std::thread([this, from, nextMode]() { runCrossfade(from, nextMode); });
}

void MusicPlayer::runCrossfade(MusicMode from, MusicMode to)
{
    const int outHandle = (from == MusicMode::CALM) ? m_handleCalm : m_handleActive;
    const int inHandle = (to == MusicMode::CALM) ? m_handleCalm : m_handleActive;

    for (int i = 0; i <= CROSSFADE_STEPS; ++i) {
        if (m_stopRequested.load()) {
            m_crossfading.store(false);
            return;
        }

        const int inVol = (128 * i) / CROSSFADE_STEPS;
        const int outVol = 128 - inVol;

        if (to == MusicMode::CALM) {
            m_volCalm.store(inVol);
            m_volActive.store(outVol);
        } else {
            m_volCalm.store(outVol);
            m_volActive.store(inVol);
        }

        m_backend->setVolume(inHandle, inVol);
        m_backend->setVolume(outHandle, outVol);

        std::this_thread::sleep_for(std::chrono::milliseconds(CROSSFADE_STEP_MS));
    }

    m_currentMode.store(to);
    m_crossfading.store(false);
    if (m_onTransition) {
        m_onTransition(to);
    }
}

