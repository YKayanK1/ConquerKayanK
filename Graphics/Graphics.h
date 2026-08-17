#pragma once
#include <windows.h>
#include <cstdint>
#include "../Resource/Resource.h"
#include <string>
#include <vector>

#ifndef GRAPHICS_EXPORTS
#define GRAPHICS_API __declspec(dllimport)
#else
#define GRAPHICS_API __declspec(dllexport)
#endif

namespace Graphics {

    // [NOVO] Memória do Rastro! Isso será guardado lá no Conquer.cpp para cada arma individualmente!
    struct ShapeOutVertex {
        float px, py, pz;
        float r, g, b, a;
        float u, v;
    };

    struct GRAPHICS_API ShapeRenderState {
        int segCount = 0;
        int segCur = 0;
        bool isFirst = true;
        float lastAx = 0, lastAy = 0, lastAz = 0;
        float lastBx = 0, lastBy = 0, lastBz = 0;
        std::vector<ShapeOutVertex> vb;

        void Initialize(int segs) {
            // No C#, SMOOTH é sempre 10. O tamanho do buffer é seg * (10 + 1) * 6 vertices
            segCount = segs * 11;
            segCur = 0;
            isFirst = true;
            vb.resize(segCount * 6);
        }
        void Reset() { isFirst = true; }
    };

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

        void DrawMesh3D(const Resource::C3Model& model, float x, float y, int textureId, int frame = 0, float angle = -0.78539f, float pitch = 0.0f, bool isPlayer = false, float scale = 1.0f, const Resource::C3Model* parentModel = nullptr, int linkBoneIndex = -1, int parentFrame = 0, int asb = 5, int adb = 6, float alpha = 1.0f, bool disableZWrite = false, int colorEnable = 0);

        void DrawParticles(const Resource::C3Model& model, float x, float y, int textureId, int frame, float angle = -0.78539f, float pitch = 0.0f, float scale = 1.0f, int asb = 5, int adb = 6, const Resource::C3Model* parentModel = nullptr, int linkBoneIndex = -1, int parentFrame = 0, int colorEnable = 0);

        // [NOVO] Renderizador da Cauda da Espada!
        void DrawShapes(const Resource::C3Model& model, ShapeRenderState& state, float x, float y, int textureId, int frame, float angle = -0.78539f, float pitch = 0.0f, float scale = 1.0f, int asb = 5, int adb = 6, const Resource::C3Model* parentModel = nullptr, int linkBoneIndex = -1, int parentFrame = 0, int colorEnable = 0, bool forceLocal = false);

        void* GetD3DDevice();
        void* GetD3DContext();

    private:
        struct Impl;
        Impl* pImpl;
    };
}