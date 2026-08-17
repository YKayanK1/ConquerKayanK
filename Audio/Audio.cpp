#include "pch.h" // Se o seu projeto estiver exigindo PCH, mantenha isso.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
// ============================================================================
// Conquer Kayank Engine - Audio Module (miniaudio)
// ============================================================================
#include "pch.h"
#include "Audio.h"
#include <iostream>

#include "miniaudio.h"

namespace Audio {

    struct Manager::Impl {
        ma_engine m_engine;
        ma_sound m_music;

        bool m_isEngineInitialized = false;
        bool m_isMusicLoaded = false;
        int m_currentVolume = 100; // Escala 0 a 100

        bool Initialize() {
            ma_result result = ma_engine_init(NULL, &m_engine);
            if (result != MA_SUCCESS) {
                std::cout << "[ERRO] Falha ao inicializar o miniaudio (Erro: " << result << ").\n";
                return false;
            }

            m_isEngineInitialized = true;
            std::cout << "[AUDIO] Motor miniaudio inicializado com sucesso!\n";
            return true;
        }

        void Destroy() {
            if (m_isMusicLoaded) {
                ma_sound_uninit(&m_music);
                m_isMusicLoaded = false;
            }
            if (m_isEngineInitialized) {
                ma_engine_uninit(&m_engine);
                m_isEngineInitialized = false;
            }
        }

        void PlayMusic(const std::string& filePath, bool loop) {
            if (!m_isEngineInitialized || filePath.empty() || filePath == "NULL") return;

            if (m_isMusicLoaded) {
                ma_sound_uninit(&m_music);
                m_isMusicLoaded = false;
            }

            ma_result result = ma_sound_init_from_file(
                &m_engine,
                filePath.c_str(),
                MA_SOUND_FLAG_STREAM,
                NULL, NULL, &m_music);

            if (result == MA_SUCCESS) {
                m_isMusicLoaded = true;
                ma_sound_set_looping(&m_music, loop ? MA_TRUE : MA_FALSE);
                SetMusicVolume(m_currentVolume);
                ma_sound_start(&m_music);
                std::cout << "[AUDIO] Tocando Musica (Stream): " << filePath << "\n";
            }
            else {
                std::cout << "[ERRO] Nao foi possivel carregar a musica: " << filePath << "\n";
            }
        }

        void StopMusic() {
            if (m_isMusicLoaded) {
                ma_sound_stop(&m_music);
            }
        }

        void SetMusicVolume(int volume) {
            if (volume < 0) volume = 0;
            if (volume > 100) volume = 100;

            m_currentVolume = volume;

            if (m_isMusicLoaded) {
                float fVol = m_currentVolume / 100.0f;
                ma_sound_set_volume(&m_music, fVol);
            }
        }

        void PlaySoundEffect(const std::string& filePath) {
            if (!m_isEngineInitialized || filePath.empty() || filePath == "NULL") return;
            ma_engine_play_sound(&m_engine, filePath.c_str(), NULL);
        }
    };
    Manager::Manager() : pImpl(new Impl()) {}
    Manager::~Manager() {
        pImpl->Destroy();
        delete pImpl;
    }

    bool Manager::Initialize() { return pImpl->Initialize(); }
    void Manager::PlayMusic(const std::string& filePath, bool loop) { pImpl->PlayMusic(filePath, loop); }
    void Manager::StopMusic() { pImpl->StopMusic(); }
    void Manager::SetMusicVolume(int volume) { pImpl->SetMusicVolume(volume); }
    void Manager::PlaySoundEffect(const std::string& filePath) { pImpl->PlaySoundEffect(filePath); }

}