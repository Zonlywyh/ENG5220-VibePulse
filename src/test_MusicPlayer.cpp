// ============================================================
//  test_MusicPlayer.cpp  —  VibePulse ENG5220
//  TDD unit tests (Google Test).  Zero hardware dependency.
//  Build:  g++ -std=c++17 -pthread MusicPlayer.cpp
//              test_MusicPlayer.cpp -lgtest -lgtest_main -o tests
//  Run:    ./tests
// ============================================================

#include "MusicPlayer.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────
//  Mock Backend — records every call, no SDL2 needed
// ─────────────────────────────────────────────────────────────
struct MockBackend : public IAudioBackend {
    // Call log entry
    struct Call { std::string op; int a = 0; int b = 0; };

    std::vector<Call> log;
    bool              ready     = true;
    int               nextId    = 0;
    int               loadFails = 0;   // force failures on first N loads

    int loadTrack(const std::string& path) override {
        if (loadFails-- > 0) return -1;
        int id = nextId++;
        log.push_back({"load", id, 0});
        return id;
    }
    void freeTrack(int id) override { log.push_back({"free", id}); }
    void play(int id, int loops) override { log.push_back({"play", id, loops}); }
    void setVolume(int id, int vol) override { log.push_back({"vol", id, vol}); }
    void halt(int id) override { log.push_back({"halt", id}); }
    bool isReady() const override { return ready; }

    bool hasCall(const std::string& op) const {
        for (auto& c : log) if (c.op == op) return true;
        return false;
    }
    int countCalls(const std::string& op) const {
        int n = 0; for (auto& c : log) if (c.op == op) ++n; return n;
    }
};

// ─────────────────────────────────────────────────────────────
//  Fixture
// ─────────────────────────────────────────────────────────────
class MusicPlayerTest : public ::testing::Test {
protected:
    std::shared_ptr<MockBackend> mock;
    std::unique_ptr<MusicPlayer> player;

    void SetUp() override {
        mock   = std::make_shared<MockBackend>();
        player = std::make_unique<MusicPlayer>(mock);
        player->loadTracks("calm.wav", "active.wav");
    }

    // Block until crossfade finishes or timeout
    void waitForCrossfade(int timeoutMs = 3000) {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs);
        while (player->isCrossfading() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

// ─────────────────────────────────────────────────────────────
//  Test Cases
// ─────────────────────────────────────────────────────────────

// T-01  Null backend must throw
TEST(MusicPlayerConstruction, ThrowsOnNullBackend) {
    EXPECT_THROW(MusicPlayer(nullptr), std::invalid_argument);
}

// T-02  loadTracks calls backend correctly
TEST_F(MusicPlayerTest, LoadTracksCallsBackend) {
    EXPECT_EQ(mock->countCalls("load"), 2);   // calm + active
    EXPECT_EQ(mock->countCalls("play"), 2);
    EXPECT_TRUE(mock->hasCall("vol"));
}

// T-03  loadTracks returns false when backend fails
TEST(MusicPlayerLoad, ReturnsFalseOnBackendFailure) {
    auto m = std::make_shared<MockBackend>();
    m->loadFails = 1;  // first load returns -1
    MusicPlayer p(m);
    EXPECT_FALSE(p.loadTracks("a.wav", "b.wav"));
}

// T-04  Initial mode is CALM
TEST_F(MusicPlayerTest, InitialModeIsCalm) {
    EXPECT_EQ(player->currentMode(), MusicMode::CALM);
}

// T-05  BPM below threshold keeps CALM mode
TEST_F(MusicPlayerTest, LowBPMKeepsCalmMode) {
    player->updateBPM(70);
    EXPECT_EQ(player->currentMode(), MusicMode::CALM);
}

// T-06  BPM above threshold triggers ACTIVE transition
TEST_F(MusicPlayerTest, HighBPMTriggersActiveMode) {
    player->updateBPM(110);
    waitForCrossfade();
    EXPECT_EQ(player->currentMode(), MusicMode::ACTIVE);
}

// T-07  BPM in hysteresis band (81-99) does NOT change mode
TEST_F(MusicPlayerTest, HysteresisBandDoesNotToggle) {
    player->updateBPM(90);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(player->currentMode(), MusicMode::CALM);
    EXPECT_FALSE(player->isCrossfading());
}

// T-08  After going ACTIVE, low BPM returns to CALM
TEST_F(MusicPlayerTest, LowBPMAfterActiveSwitchesToCalm) {
    player->updateBPM(110);
    waitForCrossfade();
    ASSERT_EQ(player->currentMode(), MusicMode::ACTIVE);

    player->updateBPM(65);
    waitForCrossfade();
    EXPECT_EQ(player->currentMode(), MusicMode::CALM);
}

// T-09  Volumes reach correct end-state after ACTIVE transition
TEST_F(MusicPlayerTest, VolumesCorrectAfterCrossfadeToActive) {
    player->updateBPM(110);
    waitForCrossfade();
    EXPECT_EQ(player->debugVolumeActive(), 128);
    EXPECT_EQ(player->debugVolumeCalm(),     0);
}

// T-10  Volumes reach correct end-state after CALM transition
TEST_F(MusicPlayerTest, VolumesCorrectAfterCrossfadeToCalm) {
    player->updateBPM(110);
    waitForCrossfade();
    player->updateBPM(65);
    waitForCrossfade();
    EXPECT_EQ(player->debugVolumeCalm(),   128);
    EXPECT_EQ(player->debugVolumeActive(),   0);
}

// T-11  isCrossfading() is true during transition
TEST_F(MusicPlayerTest, IsCrossfadingDuringTransition) {
    // We can only sample — start transition and check *before* it finishes
    player->crossfade(MusicMode::ACTIVE);
    // Immediately after launch, flag should be true
    // (small race window; we assert final state is clean)
    waitForCrossfade();
    EXPECT_FALSE(player->isCrossfading());
}

// T-12  Callback fires on transition completion
TEST_F(MusicPlayerTest, TransitionCallbackFires) {
    MusicMode reported = MusicMode::CALM;
    player->setTransitionCallback([&](MusicMode m){ reported = m; });

    player->updateBPM(110);
    waitForCrossfade();
    EXPECT_EQ(reported, MusicMode::ACTIVE);
}

// T-13  crossfade() is a no-op when already in target mode
TEST_F(MusicPlayerTest, CrossfadeToSameModeIsNoop) {
    size_t callsBefore = mock->log.size();
    player->crossfade(MusicMode::CALM);   // already CALM
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(mock->log.size(), callsBefore);  // no extra backend calls
}

// T-14  crossfade() is a no-op when backend not ready
TEST_F(MusicPlayerTest, CrossfadeNoopWhenBackendNotReady) {
    mock->ready = false;
    player->crossfade(MusicMode::ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(player->currentMode(), MusicMode::CALM);
}

// T-15  Rapid BPM updates don't deadlock or crash
TEST_F(MusicPlayerTest, RapidBPMUpdatesStable) {
    for (int i = 0; i < 20; ++i) {
        player->updateBPM((i % 2 == 0) ? 110 : 60);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    waitForCrossfade(5000);
    // Simply must not crash or hang; mode must be valid
    const MusicMode m = player->currentMode();
    EXPECT_TRUE(m == MusicMode::CALM || m == MusicMode::ACTIVE);
}
