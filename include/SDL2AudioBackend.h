// ============================================================
//  SDL2AudioBackend.h  —  VibePulse ENG5220
//  Real audio backend using SDL2_mixer.
//  Only compiled when targeting hardware; never included in tests.
// ============================================================
#pragma once
#include "MusicPlayer.h"
#include <algorithm>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <unordered_map>
#include <stdexcept>

class SDL2AudioBackend : public IAudioBackend {
public:
    SDL2AudioBackend() {
        if (SDL_Init(SDL_INIT_AUDIO) < 0)
            throw std::runtime_error(SDL_GetError());

        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
            throw std::runtime_error(Mix_GetError());

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
        if (!chunk) return -1;
        int id = m_nextId++;
        m_chunks[id] = chunk;
        // Reserve a dedicated mixer channel per track
        m_channels[id] = id;   // channel == id (works for ≤8 tracks)
        return id;
    }

    void freeTrack(int id) override {
        auto it = m_chunks.find(id);
        if (it != m_chunks.end()) {
            Mix_FreeChunk(it->second);
            m_chunks.erase(it);
            m_channels.erase(id);
        }
    }

    void play(int id, int loops) override {
        auto cit = m_chunks.find(id);
        if (cit == m_chunks.end()) return;
        int ch = Mix_PlayChannel(m_channels[id], cit->second, loops);
        m_channels[id] = ch;   // update in case SDL reassigned
    }

    void setVolume(int id, int vol) override {
        auto it = m_channels.find(id);
        if (it != m_channels.end())
            Mix_Volume(it->second, std::clamp(vol, 0, 128));
    }

    void halt(int id) override {
        auto it = m_channels.find(id);
        if (it != m_channels.end())
            Mix_HaltChannel(it->second);
    }

    bool isReady() const override { return m_ready; }

private:
    bool m_ready = false;
    int  m_nextId = 0;
    std::unordered_map<int, Mix_Chunk*> m_chunks;
    std::unordered_map<int, int>        m_channels;
};
