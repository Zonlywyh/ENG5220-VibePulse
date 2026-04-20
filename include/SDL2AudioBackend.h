// ============================================================
//  SDL2AudioBackend.h  —  VibePulse ENG5220
//  Real audio backend using SDL2_mixer.
//  Supports isFinished() for auto-advance to next track.
// ============================================================
#pragma once
#include "MusicPlayer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>

constexpr int MAX_MIXER_CHANNELS = 48;

class SDL2AudioBackend : public IAudioBackend {
public:
    SDL2AudioBackend() {
        if (SDL_Init(SDL_INIT_AUDIO) < 0)
            throw std::runtime_error(SDL_GetError());

        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
            throw std::runtime_error(Mix_GetError());

        Mix_AllocateChannels(MAX_MIXER_CHANNELS);
        m_ready = true;
    }

    ~SDL2AudioBackend() override {
        for (auto& [id, chunk] : m_chunks)
            Mix_FreeChunk(chunk);
        Mix_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    int loadTrack(const std::string& path) override {
        Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
        if (!chunk) {
            fprintf(stderr, "[SDL2] Failed to load: %s — %s\n",
                    path.c_str(), Mix_GetError());
            return -1;
        }
        int id = m_nextId++;
        m_chunks[id]   = chunk;
        m_channels[id] = id;
        return id;
    }

    void freeTrack(int id) override {
        auto it = m_chunks.find(id);
        if (it != m_chunks.end()) {
            Mix_HaltChannel(m_channels[id]);
            Mix_FreeChunk(it->second);
            m_chunks.erase(it);
            m_channels.erase(id);
        }
    }

    // loops=0 → play once and stop; loops=-1 → loop forever
    void play(int id, int loops) override {
        auto cit = m_chunks.find(id);
        if (cit == m_chunks.end()) return;
        int ch = Mix_PlayChannel(m_channels[id], cit->second, loops);
        if (ch >= 0) m_channels[id] = ch;
    }

    void setVolume(int id, int vol) override {
        auto it = m_channels.find(id);
        if (it != m_channels.end())
            Mix_Volume(it->second, std::clamp(vol, 0, MIX_MAX_VOLUME));
    }

    void halt(int id) override {
        auto it = m_channels.find(id);
        if (it != m_channels.end())
            Mix_HaltChannel(it->second);
    }

    bool isFinished(int id) const override {
        auto it = m_channels.find(id);
        if (it == m_channels.end()) return true;
        return Mix_Playing(it->second) == 0;
    }

    bool isReady() const override { return m_ready; }

private:
    bool m_ready  = false;
    int  m_nextId = 0;
    std::unordered_map<int, Mix_Chunk*> m_chunks;
    std::unordered_map<int, int>        m_channels;
};
