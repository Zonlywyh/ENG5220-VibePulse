// ============================================================
//  MusicPlayer.cpp  —  VibePulse ENG5220
// ============================================================

#include "MusicPlayer.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────
//  Ctor / Dtor
// ─────────────────────────────────────────────────────────────
MusicPlayer::MusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend)
        throw std::invalid_argument("MusicPlayer: backend must not be null");
}

MusicPlayer::~MusicPlayer() {
    // Signal any running crossfade to stop, then join
    m_stopRequested.store(true);
    if (m_worker.joinable())
        m_worker.join();

    // Release loaded tracks
    if (m_handleCalm   >= 0) m_backend->freeTrack(m_handleCalm);
    if (m_handleActive >= 0) m_backend->freeTrack(m_handleActive);
}

// ─────────────────────────────────────────────────────────────
//  Load
// ─────────────────────────────────────────────────────────────
bool MusicPlayer::loadTracks(const std::string& calmPath,
                              const std::string& activePath) {
    m_handleCalm   = m_backend->loadTrack(calmPath);
    m_handleActive = m_backend->loadTrack(activePath);

    if (m_handleCalm < 0 || m_handleActive < 0)
        return false;

    // Start calm track at full volume; active track silent
    m_backend->play(m_handleCalm,   -1);
    m_backend->play(m_handleActive, -1);
    m_backend->setVolume(m_handleCalm,   128);
    m_backend->setVolume(m_handleActive,   0);
    m_volCalm.store(128);
    m_volActive.store(0);
    return true;
}

// ─────────────────────────────────────────────────────────────
//  BPM → mode mapping  (hysteresis prevents rapid toggling)
// ─────────────────────────────────────────────────────────────
void MusicPlayer::updateBPM(int bpm) {
    const MusicMode current = m_currentMode.load();

    if (current == MusicMode::CALM  && bpm >= BPM_ACTIVE_THRESHOLD) {
        crossfade(MusicMode::ACTIVE);
    } else if (current == MusicMode::ACTIVE && bpm <= BPM_CALM_THRESHOLD) {
        crossfade(MusicMode::CALM);
    }
    // In-between zone: stay in current mode (hysteresis band 81-99 BPM)
}

// ─────────────────────────────────────────────────────────────
//  Non-blocking crossfade launcher
// ─────────────────────────────────────────────────────────────
void MusicPlayer::crossfade(MusicMode nextMode) {
    if (m_currentMode.load() == nextMode) return;    // already there
    if (!m_backend->isReady())            return;    // audio not initialised

    // Interrupt any in-flight crossfade
    m_stopRequested.store(true);
    if (m_worker.joinable())
        m_worker.join();

    m_stopRequested.store(false);
    m_crossfading.store(true);

    const MusicMode from = m_currentMode.load();
    // Launch detached — ownership transferred into lambda
    m_worker = std::thread([this, from, nextMode]() {
        runCrossfade(from, nextMode);
    });
    m_worker.detach();   // fire-and-forget; state is managed atomically
}

// ─────────────────────────────────────────────────────────────
//  Crossfade worker  (runs on a background thread)
// ─────────────────────────────────────────────────────────────
void MusicPlayer::runCrossfade(MusicMode from, MusicMode to) {
    // Determine which handle is fading in / out
    const int hIn  = (to == MusicMode::ACTIVE) ? m_handleActive : m_handleCalm;
    const int hOut = (to == MusicMode::ACTIVE) ? m_handleCalm   : m_handleActive;

    for (int step = 1; step <= CROSSFADE_STEPS; ++step) {
        if (m_stopRequested.load()) {
            // Interrupted — leave volumes where they are; caller will reset
            m_crossfading.store(false);
            return;
        }

        const int volIn  = static_cast<int>((128.0f * step) / CROSSFADE_STEPS);
        const int volOut = 128 - volIn;

        m_backend->setVolume(hIn,  volIn);
        m_backend->setVolume(hOut, volOut);

        // Track atomic volume mirrors (readable by tests)
        if (to == MusicMode::ACTIVE) {
            m_volActive.store(volIn);
            m_volCalm.store(volOut);
        } else {
            m_volCalm.store(volIn);
            m_volActive.store(volOut);
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(CROSSFADE_STEP_MS));
    }

    // Guarantee clean end-state
    m_backend->setVolume(hIn,  128);
    m_backend->setVolume(hOut,   0);

    if (to == MusicMode::ACTIVE) { m_volActive.store(128); m_volCalm.store(0); }
    else                         { m_volCalm.store(128);   m_volActive.store(0); }

    m_currentMode.store(to);
    m_crossfading.store(false);

    if (m_onTransition) m_onTransition(to);
}

// ─────────────────────────────────────────────────────────────
//  Callback setter
// ─────────────────────────────────────────────────────────────
void MusicPlayer::setTransitionCallback(std::function<void(MusicMode)> cb) {
    m_onTransition = std::move(cb);
}
