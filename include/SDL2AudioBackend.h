// ============================================================
//  SDL2AudioBackend.h  —  VibePulse ENG5220
//  Real audio backend using SDL2_mixer.
//  Only compiled when targeting hardware; never included in tests.
// ============================================================
#pragma once
#include "MusicPlayer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>

// 最多同时加载的曲目数（6个zone × 每zone最多8首 = 48）
// SDL2_mixer 需要预先分配足够的混音通道
constexpr int MAX_MIXER_CHANNELS = 48;

class SDL2AudioBackend : public IAudioBackend {
public:
    SDL2AudioBackend() {
        if (SDL_Init(SDL_INIT_AUDIO) < 0)
            throw std::runtime_error(SDL_GetError());

        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
            throw std::runtime_error(Mix_GetError());

        // 分配足够的混音通道，覆盖所有曲目
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
            // 打印具体失败原因，方便调试
            fprintf(stderr, "[SDL2] Failed to load: %s — %s\n",
                    path.c_str(), Mix_GetError());
            return -1;
        }
        int id = m_nextId++;
        m_chunks[id]   = chunk;
        m_channels[id] = id;   // 每首歌独占一个通道
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

    bool isReady() const override { return m_ready; }

private:
    bool m_ready   = false;
    int  m_nextId  = 0;
    std::unordered_map<int, Mix_Chunk*> m_chunks;
    std::unordered_map<int, int>        m_channels;
};
