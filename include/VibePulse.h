/**
 * @file VibePulse.h
 * @brief Global interface definitions for the VibePulse project.
 * * This file defines the communication protocols between the sensor, 
 * the analyzer, and the music player modules to ensure seamless integration.
 */

#ifndef VIBEPULSE_H
#define VIBEPULSE_H

#include <vector>
#include <functional>

/**
 * @class HeartRateSensor
 * @brief Handles low-level I2C communication with the MAX30102 hardware.
 * Responsibility: Member A
 */
class HeartRateSensor {
public:
    // Callback type for asynchronous data delivery [cite: 38]
    using DataCallback = std::function<void(const std::vector<int>&)>;

    /**
     * @brief Registers a callback to handle raw sensor data.
     * This follows an event-driven design to avoid polling[cite: 8, 72].
     */
    void setDataCallback(DataCallback cb);

    /**
     * @brief Starts the hardware data acquisition in a dedicated thread.
     * Ensures high reliability and realtime responsiveness.
     */
    void start();
};

/**
 * @class BPMAnalyzer
 * @brief Processes raw signals to calculate a stable heart rate (BPM).
 * Responsibility: Member B
 */
class BPMAnalyzer {
public:
    /**
     * @brief Digital Signal Processing (DSP) logic to detect pulse peaks.
     * @param data Raw IR/Red light values from the sensor.
     */
    void processRawData(const std::vector<int>& data);

    // Callback type to notify the player of BPM changes
    using BPMCallback = std::function<void(int)>;

    /**
     * @brief Registers a callback to be triggered when a new BPM is calculated.
     */
    void setBPMCallback(BPMCallback cb);
};

/**
 * @class MusicPlayer
 * @brief Manages the audio environment and smooth track transitions.
 * Responsibility: Nan Mengfei
 */
class MusicPlayer {
public:
    /**
     * @brief Triggered by BPM updates to decide music selection.
     * @param currentBPM The latest calculated heart rate.
     */
    void onBPMUpdate(int currentBPM);

    /**
     * @brief Implements smooth crossfading between tracks.
     * Ensures the transition is non-blocking to maintain realtime operation[cite: 70].
     * @param nextTrackIndex The index of the next song to play.
     */
    void crossfadeToNext(int nextTrackIndex);

    /**
     * @brief Main loop for the audio engine.
     */
    void run();
};

#endif
