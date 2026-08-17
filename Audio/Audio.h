// ============================================================================
// Conquer Kayank Engine - Audio Module (miniaudio)
// ============================================================================
#pragma once
#include <string>

#ifndef AUDIO_EXPORTS
#define AUDIO_API __declspec(dllimport)
#else
#define AUDIO_API __declspec(dllexport)
#endif

namespace Audio {

    class AUDIO_API Manager {
    public:
        Manager();
        ~Manager();

        bool Initialize();

        void PlayMusic(const std::string& filePath, bool loop = true);

        void StopMusic();

        void SetMusicVolume(int volume);

        void PlaySoundEffect(const std::string& filePath);

    private:
        struct Impl;
        Impl* pImpl;
    };
}