// ============================================================
//  test_MusicPlayer.cpp  —  VibePulse ENG5220  (6-zone)
//  Google Test unit tests. Zero hardware dependency.
// ============================================================

#include "../include/ZoneMusicPlayer.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>

struct MockBackend : public IAudioBackend {
    struct Call { std::string op; int a = 0; int b = 0; };
    std::vector<Call> log;
    bool ready       = true;
    int  nextId      = 0;
    int  loadFails   = 0;

    std::atomic<int> finishedHandle{-1};
    std::function<void(int)> finishedCallback;

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
    void setTrackFinishedCallback(std::function<void(int)> cb) override {
        finishedCallback = std::move(cb);
    }

    void finishTrack(int id) {
        finishedHandle.store(id);
        if (finishedCallback) {
            finishedCallback(id);
        }
    }

    int countCalls(const std::string& op) const {
        int n = 0;
        for (auto& c : log) if (c.op == op) ++n;
        return n;
    }
};

class MusicPlayerTest : public ::testing::Test {
protected:
    std::shared_ptr<MockBackend>     mock;
    std::unique_ptr<ZoneMusicPlayer> player;

    void SetUp() override {
        mock   = std::make_shared<MockBackend>();
        player = std::make_unique<ZoneMusicPlayer>(mock);
        for (int z = 1; z <= 6; ++z)
            player->loadZone(z, {"z" + std::to_string(z) + "a.wav",
                                  "z" + std::to_string(z) + "b.wav"});
    }

    void waitForCrossfade(int timeoutMs = 3000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (player->isCrossfading() &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

TEST(Construction, ThrowsOnNullBackend) {
    EXPECT_THROW(ZoneMusicPlayer(nullptr), std::invalid_argument);
}

TEST_F(MusicPlayerTest, LoadZoneLoadsAllTracks) {
    EXPECT_EQ(mock->countCalls("load"), 12);
}

TEST(MusicPlayerLoad, ReturnsFalseOnBackendFailure) {
    auto m = std::make_shared<MockBackend>();
    m->loadFails = 1;
    ZoneMusicPlayer p(m);
    EXPECT_FALSE(p.loadZone(1, {"a.wav", "b.wav"}));
}

TEST_F(MusicPlayerTest, InitialZoneIsZone1) {
    EXPECT_EQ(player->currentZone(), 1);
}

TEST(BpmMapping, AllZoneBoundaries) {
    EXPECT_EQ(bpmToZone(79),  1);
    EXPECT_EQ(bpmToZone(80),  2);
    EXPECT_EQ(bpmToZone(99),  2);
    EXPECT_EQ(bpmToZone(100), 3);
    EXPECT_EQ(bpmToZone(119), 3);
    EXPECT_EQ(bpmToZone(120), 4);
    EXPECT_EQ(bpmToZone(139), 4);
    EXPECT_EQ(bpmToZone(140), 5);
    EXPECT_EQ(bpmToZone(159), 5);
    EXPECT_EQ(bpmToZone(160), 6);
}

TEST_F(MusicPlayerTest, BPM65StaysInZone1) {
    player->updateBPM(65);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(player->currentZone(), 1);
    EXPECT_FALSE(player->isCrossfading());
}

TEST_F(MusicPlayerTest, BPM85TransitionsToZone2) {
    player->updateBPM(85);
    waitForCrossfade();
    EXPECT_EQ(player->currentZone(), 2);
}

TEST_F(MusicPlayerTest, BPM170TransitionsToZone6) {
    player->updateBPM(170);
    waitForCrossfade();
    EXPECT_EQ(player->currentZone(), 6);
}

TEST_F(MusicPlayerTest, BPMDropFromZone6ToZone1) {
    player->updateBPM(170); waitForCrossfade();
    player->updateBPM(65);  waitForCrossfade();
    EXPECT_EQ(player->currentZone(), 1);
}

TEST_F(MusicPlayerTest, VolumesCorrectAfterCrossfade) {
    player->updateBPM(85);
    waitForCrossfade();
    EXPECT_EQ(player->debugVolumeIn(),  128);
    EXPECT_EQ(player->debugVolumeOut(),   0);
}

TEST_F(MusicPlayerTest, NotCrossfadingAfterTransition) {
    player->setZone(3);
    waitForCrossfade();
    EXPECT_FALSE(player->isCrossfading());
}

TEST_F(MusicPlayerTest, TransitionCallbackFires) {
    int reported = 0;
    player->setTransitionCallback([&](int z){ reported = z; });
    player->updateBPM(85);
    waitForCrossfade();
    EXPECT_EQ(reported, 2);
}

TEST_F(MusicPlayerTest, SetZoneToSameZoneIsNoop) {
    size_t before = mock->log.size();
    player->setZone(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(mock->log.size(), before);
}

TEST_F(MusicPlayerTest, CrossfadeNoopWhenBackendNotReady) {
    mock->ready = false;
    player->setZone(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(player->currentZone(), 1);
}

TEST_F(MusicPlayerTest, AutoAdvancesWhenTrackFinishes) {
    int playsBefore = mock->countCalls("play");
    mock->finishTrack(0);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(1000);
    while (mock->countCalls("play") == playsBefore &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_GT(mock->countCalls("play"), playsBefore);
    EXPECT_EQ(player->currentZone(), 1);
}

TEST_F(MusicPlayerTest, RapidBPMUpdatesStable) {
    int bpms[] = {65, 85, 105, 125, 150, 170, 130, 95, 70, 160};
    for (int b : bpms) {
        player->updateBPM(b);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    waitForCrossfade(5000);
    int z = player->currentZone();
    EXPECT_TRUE(z >= 1 && z <= 6);
}
