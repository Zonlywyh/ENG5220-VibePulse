#include "MusicPlayer.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Minimal, dependency-free test runner for CTest.

static int g_failures = 0;

static void expectTrue(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << std::endl;
        ++g_failures;
    }
}

static void expectEqInt(int a, int b, const std::string& msg) {
    if (a != b) {
        std::cerr << "[FAIL] " << msg << " (expected " << b << ", got " << a << ")" << std::endl;
        ++g_failures;
    }
}

// Mock backend — records every call, no SDL2 needed.
struct MockBackend : public IAudioBackend {
    struct Call { std::string op; int a = 0; int b = 0; };
    std::vector<Call> log;

    bool ready = true;
    int nextId = 0;
    int loadFails = 0; // force failures on first N loads

    int loadTrack(const std::string&) override {
        if (loadFails-- > 0) return -1;
        const int id = nextId++;
        log.push_back({"load", id, 0});
        return id;
    }
    void freeTrack(int id) override { log.push_back({"free", id, 0}); }
    void play(int id, int loops) override { log.push_back({"play", id, loops}); }
    void setVolume(int id, int vol) override { log.push_back({"vol", id, vol}); }
    void halt(int id) override { log.push_back({"halt", id, 0}); }
    bool isReady() const override { return ready; }

    int countCalls(const std::string& op) const {
        int n = 0;
        for (const auto& c : log) if (c.op == op) ++n;
        return n;
    }
};

static void waitForCrossfade(const MusicPlayer& p, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (p.isCrossfading() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void testNullBackendThrows() {
    bool threw = false;
    try {
        MusicPlayer p(nullptr);
        (void)p;
    } catch (...) {
        threw = true;
    }
    expectTrue(threw, "MusicPlayer(nullptr) should throw");
}

static void testLoadTracksCallsBackend() {
    auto m = std::make_shared<MockBackend>();
    MusicPlayer p(m);
    const bool ok = p.loadTracks("calm.wav", "active.wav");
    expectTrue(ok, "loadTracks should succeed when backend loads succeed");
    expectEqInt(m->countCalls("load"), 2, "loadTracks should load 2 tracks");
    expectEqInt(m->countCalls("play"), 2, "loadTracks should play 2 tracks");
    expectTrue(m->countCalls("vol") >= 2, "loadTracks should set initial volumes");
}

static void testLoadTracksFailsIfBackendFails() {
    auto m = std::make_shared<MockBackend>();
    m->loadFails = 1;
    MusicPlayer p(m);
    const bool ok = p.loadTracks("a.wav", "b.wav");
    expectTrue(!ok, "loadTracks should fail if backend fails a load");
}

static void testBpmTriggersModeChange() {
    auto m = std::make_shared<MockBackend>();
    MusicPlayer p(m);
    expectTrue(p.loadTracks("calm.wav", "active.wav"), "loadTracks ok");

    // Normal path: high BPM should enter ACTIVE.
    p.updateBPM(110);
    waitForCrossfade(p);
    expectTrue(p.currentMode() == MusicMode::ACTIVE, "BPM 110 should switch to ACTIVE");

    // Boundary / hysteresis: mid band should not toggle back.
    p.updateBPM(90);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expectTrue(p.currentMode() == MusicMode::ACTIVE, "BPM 90 should not exit ACTIVE (hysteresis)");

    // Error-ish path: backend not ready should block transitions.
    m->ready = false;
    p.updateBPM(60); // would want CALM, but backend is not ready so no crossfade
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expectTrue(p.currentMode() == MusicMode::ACTIVE, "Backend not ready should prevent transition");

    // Restore and allow transition down to CALM.
    m->ready = true;
    p.updateBPM(60);
    waitForCrossfade(p);
    expectTrue(p.currentMode() == MusicMode::CALM, "BPM 60 should switch to CALM");
}

int main() {
    testNullBackendThrows();
    testLoadTracksCallsBackend();
    testLoadTracksFailsIfBackendFails();
    testBpmTriggersModeChange();

    if (g_failures == 0) {
        std::cout << "[OK] All tests passed" << std::endl;
        return 0;
    }
    std::cerr << "[FAIL] " << g_failures << " test(s) failed" << std::endl;
    return 1;
}
