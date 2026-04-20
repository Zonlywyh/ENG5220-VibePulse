// ============================================================
//  SDL2AudioBackend.h - VibePulse ENG5220
//  Real audio backend using SDL2_mixer.
//  Supports completion callbacks for event-driven auto-advance.
// ============================================================
#pragma once

#include "MusicPlayer.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

constexpr int MAX_MIXER_CHANNELS = 48;

class SDL2AudioBackend : public IAudioBackend {
public:
    SDL2AudioBackend() {
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            throw std::runtime_error(SDL_GetError());
        }

        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
            throw std::runtime_error(Mix_GetError());
        }

        Mix_AllocateChannels(MAX_MIXER_CHANNELS);
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_instance = this;
        }
        Mix_ChannelFinished(&SDL2AudioBackend::onChannelFinished);
        m_ready = true;
    }

    ~SDL2AudioBackend() override {
        Mix_ChannelFinished(nullptr);
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            if (s_instance == this) {
                s_instance = nullptr;
            }
        }

        std::unordered_map<int, Mix_Chunk*> chunksToFree;
        {
            std::lock_guard<std::mutex> stateLock(m_stateMutex);
            chunksToFree.swap(m_chunks);
            m_channels.clear();
        }

        for (auto& [id, chunk] : chunksToFree) {
            (void)id;
            Mix_FreeChunk(chunk);
        }

        Mix_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    int loadTrack(const std::string& path) override {
        Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
        if (!chunk) {
            std::fprintf(stderr, "[SDL2] Failed to load: %s - %s\n",
                         path.c_str(), Mix_GetError());
            return -1;
        }

        std::lock_guard<std::mutex> lock(m_stateMutex);
        const int id = m_nextId++;
        m_chunks[id] = chunk;
        m_channels[id] = id;
        return id;
    }

    void freeTrack(int id) override {
        int channel = -1;
        Mix_Chunk* chunk = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto chunkIt = m_chunks.find(id);
            auto channelIt = m_channels.find(id);
            if (chunkIt == m_chunks.end() || channelIt == m_channels.end()) {
                return;
            }

            chunk = chunkIt->second;
            channel = channelIt->second;
            m_chunks.erase(chunkIt);
            m_channels.erase(channelIt);
        }

        if (channel >= 0) {
            Mix_HaltChannel(channel);
        }
        if (!chunk) {
            return;
        }
        Mix_FreeChunk(chunk);
    }

    void play(int id, int loops) override {
        int preferredChannel = -1;
        Mix_Chunk* chunk = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto chunkIt = m_chunks.find(id);
            auto channelIt = m_channels.find(id);
            if (chunkIt == m_chunks.end() || channelIt == m_channels.end()) {
                return;
            }

            chunk = chunkIt->second;
            preferredChannel = channelIt->second;
        }

        const int ch = Mix_PlayChannel(preferredChannel, chunk, loops);
        if (ch < 0) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_channels[id] = ch;
        }
    }

    void setVolume(int id, int vol) override {
        int channel = -1;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto it = m_channels.find(id);
            if (it != m_channels.end()) {
                channel = it->second;
            }
        }

        if (channel >= 0) {
            Mix_Volume(channel, std::clamp(vol, 0, MIX_MAX_VOLUME));
        }
    }

    void halt(int id) override {
        int channel = -1;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto it = m_channels.find(id);
            if (it != m_channels.end()) {
                channel = it->second;
            }
        }

        if (channel >= 0) {
            Mix_HaltChannel(channel);
        }
    }

    bool isFinished(int id) const override {
        int channel = -1;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto it = m_channels.find(id);
            if (it != m_channels.end()) {
                channel = it->second;
            }
        }

        if (channel < 0) {
            return true;
        }
        return Mix_Playing(channel) == 0;
    }

    bool isReady() const override { return m_ready; }

    void setTrackFinishedCallback(std::function<void(int)> cb) override {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_trackFinishedCallback = std::move(cb);
    }

private:
    static void onChannelFinished(int channel) {
        SDL2AudioBackend* instance = nullptr;
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            instance = s_instance;
        }
        if (instance) {
            instance->dispatchTrackFinished(channel);
        }
    }

    void dispatchTrackFinished(int channel) {
        std::function<void(int)> callback;
        {
            std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
            callback = m_trackFinishedCallback;
        }
        if (!callback) {
            return;
        }

        int finishedId = -1;
        {
            std::lock_guard<std::mutex> stateLock(m_stateMutex);
            for (const auto& [id, assignedChannel] : m_channels) {
                if (assignedChannel == channel) {
                    finishedId = id;
                    break;
                }
            }
        }

        if (finishedId >= 0) {
            callback(finishedId);
        }
    }

    bool m_ready = false;
    int m_nextId = 0;
    std::unordered_map<int, Mix_Chunk*> m_chunks;
    std::unordered_map<int, int> m_channels;
    mutable std::mutex m_stateMutex;
    std::mutex m_callbackMutex;
    std::function<void(int)> m_trackFinishedCallback;

    inline static std::mutex s_callbackMutex;
    inline static SDL2AudioBackend* s_instance = nullptr;
};
