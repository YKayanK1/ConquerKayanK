#pragma once
#include <windows.h>
#include <cstdint>
#include "../Resource/Resource.h"
#include <string>

#ifndef GRAPHICS_EXPORTS
#define GRAPHICS_API __declspec(dllimport)
#else
#define GRAPHICS_API __declspec(dllexport)
#endif

namespace Graphics {
    class GRAPHICS_API SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer();

        void Initialize(HWND hwnd, int width, int height);
        void Resize(int width, int height);
        void BeginFrame();
        void EndFrame();

        void DrawSprite(int textureId, int x, int y, int width, int height);
        void LoadTexture(const wchar_t* filename);
        int LoadTextureFromMemory(const uint8_t* data, size_t size);
        void DeleteTexture(int id);

        void DrawMesh3D(const Resource::C3Model& model, float x, float y, int textureId, int frame = 0, float angle = -0.78539f, float pitch = 0.0f, bool isPlayer = false, float scale = 1.0f, const Resource::C3Model* parentModel = nullptr, int linkBoneIndex = -1, const std::string& effectName = "", int asb = 5, int adb = 6, float alpha = 1.0f, bool disableZWrite = false);

        // [CORRIGIDO] Adicionamos o "float pitch" para as partículas também poderem deitar/levantar!
        void DrawParticles(const Resource::C3Model& model, float x, float y, int textureId, int frame, float angle = -0.78539f, float pitch = 0.0f, float scale = 1.0f, int asb = 5, int adb = 6);

        // [NOVO] Ponte segura para o ImGui pegar o DirectX da DLL
        void* GetD3DDevice();
        void* GetD3DContext();

    private:
        struct Impl;
        Impl* pImpl;
    };
}