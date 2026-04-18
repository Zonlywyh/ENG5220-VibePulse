// ============================================================
//  test_MusicPlayer.cpp  —  VibePulse ENG5220  (6-zone)
//  Google Test unit tests. Zero hardware dependency.
// ============================================================

#include "MusicPlayer.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

// ─────────────────────────────────────────────────────────────
//  Mock Backend
// ─────────────────────────────────────────────────────────────
struct MockBackend : public IAudioBackend {
    struct Call { std::string op; int a = 0; int b = 0; };
    std::vector<Call> log;
    bool ready       = true;
    int  nextId      = 0;
    int  loadFails   = 0;

    // Controls which track isFinished() returns true for
    std::atomic<int> finishedHandle{-1};

    int  loadTrack(const std::string&) override {
        if (loadFails-- > 0) return -1;
        int id = nextId++;
        log.push_back({"load", id});
        return id;
    }
    void freeTrack(int id)          override { log.push_back({"free", id}); }
    void play(int id, int loops)    override { log.push_back({"play", id, loops}); }
    void setVolume(int id, int vol) override { log.push_back({"vol",  id, vol}); }
    void halt(int id)               override { log.push_back({"halt", id}); }
    bool isReady() const            override { return ready; }
    bool isFinished(int id) const   override { return finishedHandle.load() == id; }

    int countCalls(const std::string& op) const {
        int n = 0;
        for (auto& c : log) if (c.op == op) ++n;
        return n;
    }
    bool hasCallWith(const std::string& op, int a) const {
        for (auto& c : log) if (c.op == op && c.a == a) return true;
        return false;
    }
};

// ─────────────────────────────────────────────────────────────
//  Fixture — 2 tracks per zone × 6 zones = 12 tracks
// ─────────────────────────────────────────────────────────────
class MusicPlayerTest : public ::testing::Test {
protected:
    std::shared_ptr<MockBackend> mock;
    std::unique_ptr<MusicPlayer> player;

    void SetUp() override {
        mock   = std::make_shared<MockBackend>();
        player = std::make_unique<MusicPlayer>(mock);
        player->loadZone(MusicZone::ZONE_1, {"z1a.wav", "z1b.wav"});
        player->loadZone(MusicZone::ZONE_2, {"z2a.wav", "z2b.wav"});
        player->loadZone(MusicZone::ZONE_3, {"z3a.wav", "z3b.wav"});
        player->loadZone(MusicZone::ZONE_4, {"z4a.wav", "z4b.wav"});
        player->loadZone(MusicZone::ZONE_5, {"z5a.wav", "z5b.wav"});
        player->loadZone(MusicZone::ZONE_6, {"z6a.wav", "z6b.wav"});
    }

    void waitForCrossfade(int timeoutMs = 3000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (player->isCrossfading() &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

// ── T-01  Null backend throws ─────────────────────────────────
TEST(Construction, ThrowsOnNullBackend) {
    EXPECT_THROW(MusicPlayer(nullptr), std::invalid_argument);
}

// ── T-02  All tracks loaded ───────────────────────────────────
TEST_F(MusicPlayerTest, LoadZoneLoadsAllTracks) {
    EXPECT_EQ(mock->countCalls("load"), 12);
}

// ── T-03  Load failure returns false ─────────────────────────
TEST(MusicPlayerLoad, ReturnsFalseOnBackendFailure) {
    auto m = std::make_shared<MockBackend>();
    m->loadFails = 1;
    MusicPlayer p(m);
    EXPECT_FALSE(p.loadZone(MusicZone::ZONE_1, {"a.wav", "b.wav"}));
}

// ── T-04  Initial zone is ZONE_1 ──────────────────────────────
TEST_F(MusicPlayerTest, InitialZoneIsZone1) {
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_1);
}

// ── T-05  BPM boundary mapping ────────────────────────────────
TEST(BpmMapping, AllZoneBoundaries) {
    EXPECT_EQ(bpmToZone(60),  MusicZone::ZONE_1);
    EXPECT_EQ(bpmToZone(79),  MusicZone::ZONE_1);
    EXPECT_EQ(bpmToZone(80),  MusicZone::ZONE_2);
    EXPECT_EQ(bpmToZone(99),  MusicZone::ZONE_2);
    EXPECT_EQ(bpmToZone(100), MusicZone::ZONE_3);
    EXPECT_EQ(bpmToZone(119), MusicZone::ZONE_3);
    EXPECT_EQ(bpmToZone(120), MusicZone::ZONE_4);
    EXPECT_EQ(bpmToZone(139), MusicZone::ZONE_4);
    EXPECT_EQ(bpmToZone(140), MusicZone::ZONE_5);
    EXPECT_EQ(bpmToZone(159), MusicZone::ZONE_5);
    EXPECT_EQ(bpmToZone(160), MusicZone::ZONE_6);
    EXPECT_EQ(bpmToZone(180), MusicZone::ZONE_6);
}

// ── T-06  Same BPM zone, no crossfade ────────────────────────
TEST_F(MusicPlayerTest, BPM65StaysInZone1) {
    player->updateBPM(65);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_1);
    EXPECT_FALSE(player->isCrossfading());
}

// ── T-07  BPM 85 → Zone 2 ────────────────────────────────────
TEST_F(MusicPlayerTest, BPM85TransitionsToZone2) {
    player->updateBPM(85);
    waitForCrossfade();
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_2);
}

// ── T-08  BPM 170 → Zone 6 ───────────────────────────────────
TEST_F(MusicPlayerTest, BPM170TransitionsToZone6) {
    player->updateBPM(170);
    waitForCrossfade();
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_6);
}

// ── T-09  Zone 6 → Zone 1 ────────────────────────────────────
TEST_F(MusicPlayerTest, BPMDropFromZone6ToZone1) {
    player->updateBPM(170); waitForCrossfade();
    player->updateBPM(65);  waitForCrossfade();
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_1);
}

// ── T-10  Volumes correct after crossfade ────────────────────
TEST_F(MusicPlayerTest, VolumesCorrectAfterCrossfade) {
    player->updateBPM(85);
    waitForCrossfade();
    EXPECT_EQ(player->debugVolumeIn(),  128);
    EXPECT_EQ(player->debugVolumeOut(),   0);
}

// ── T-11  Not crossfading after transition ───────────────────
TEST_F(MusicPlayerTest, NotCrossfadingAfterTransition) {
    player->crossfadeTo(MusicZone::ZONE_3);
    waitForCrossfade();
    EXPECT_FALSE(player->isCrossfading());
}

// ── T-12  Callback fires on zone transition ──────────────────
TEST_F(MusicPlayerTest, TransitionCallbackFires) {
    MusicZone reported = MusicZone::ZONE_1;
    player->setTransitionCallback([&](MusicZone z){ reported = z; });
    player->updateBPM(85);
    waitForCrossfade();
    EXPECT_EQ(reported, MusicZone::ZONE_2);
}

// ── T-13  Same zone crossfade is no-op ───────────────────────
TEST_F(MusicPlayerTest, CrossfadeToSameZoneIsNoop) {
    size_t before = mock->log.size();
    player->crossfadeTo(MusicZone::ZONE_1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(mock->log.size(), before);
}

// ── T-14  No-op when backend not ready ───────────────────────
TEST_F(MusicPlayerTest, CrossfadeNoopWhenBackendNotReady) {
    mock->ready = false;
    player->crossfadeTo(MusicZone::ZONE_2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_1);
}

// ── T-15  Auto-advance when track finishes ───────────────────
TEST_F(MusicPlayerTest, AutoAdvancesWhenTrackFinishes) {
    // Zone 1 starts with track 0, simulate it finishing
    int playsBefore = mock->countCalls("play");
    mock->finishedHandle.store(0);  // track 0 = first zone1 track

    // Wait for monitor thread to detect and switch (max 1s)
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(1000);
    while (mock->countCalls("play") == playsBefore &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // A new play call should have been made
    EXPECT_GT(mock->countCalls("play"), playsBefore);
    // Zone should remain the same
    EXPECT_EQ(player->currentZone(), MusicZone::ZONE_1);
}

// ── T-16  Rapid BPM updates stable ───────────────────────────
TEST_F(MusicPlayerTest, RapidBPMUpdatesStable) {
    int bpms[] = {65, 85, 105, 125, 150, 170, 130, 95, 70, 160};
    for (int b : bpms) {
        player->updateBPM(b);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    waitForCrossfade(5000);
    auto z = player->currentZone();
    EXPECT_TRUE(z >= MusicZone::ZONE_1 && z <= MusicZone::ZONE_6);
}
