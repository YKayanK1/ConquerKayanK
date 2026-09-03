#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

#ifndef GRAPHICS_RIOT_EXPORTS
#define RIOT_API __declspec(dllimport)
#else
#define RIOT_API __declspec(dllexport)
#endif

namespace GraphicsRiot {

    using RiotModelHandle = uint32_t;
    constexpr RiotModelHandle InvalidRiotModelHandle = 0;

    // Must be called once with the existing D3D11 device/context from Graphics::SceneRenderer
    // (GetD3DDevice()/GetD3DContext()) before loading or drawing any champion model.
    // screenWidth/screenHeight are used to build the same screen-space orthographic projection
    // used by Graphics::DrawMesh3D, so champions line up with the rest of the 3D scene.
    RIOT_API void Initialize(void* d3dDevice, void* d3dContext, int screenWidth, int screenHeight);

    RIOT_API void Resize(int screenWidth, int screenHeight);

    // Loads a champion as a monster-like model from loose .skn/.skl/.anm files plus a texture
    // (.dds or .tex, detected automatically by extension). anmPath may be empty for a static bind pose.
    // The .skn's submeshes (ex.: body, weapon, particle props) all share this single texture.
    RIOT_API RiotModelHandle LoadRiotChampion(const char* sknPath, const char* sklPath,
        const char* anmPath, const wchar_t* texturePath);

    RIOT_API void UnloadRiotChampion(RiotModelHandle handle);

    // Number of submeshes ("Meshes" list, ex.: "Zoe_Base_Mat", "Zoe_Base_Yoyo_Mat", etc.)
    // found inside the loaded .skn, mirroring the reference LoL viewer's per-object list.
    RIOT_API int GetRiotSubMeshCount(RiotModelHandle handle);
    RIOT_API const char* GetRiotSubMeshName(RiotModelHandle handle, int subMeshIndex);

    // Shows/hides an individual submesh (ex.: turn off a champion's held weapon/prop object).
    RIOT_API void SetRiotSubMeshVisible(RiotModelHandle handle, int subMeshIndex, bool visible);
    RIOT_API bool IsRiotSubMeshVisible(RiotModelHandle handle, int subMeshIndex);

    // Overrides the texture used by a single submesh (ex.: assign a different .tex/.dds found in
    // the same skin folder to the weapon/prop object). Pass an empty path to fall back to the
    // model's default texture (the one passed to LoadRiotChampion).
    RIOT_API bool SetRiotSubMeshTexture(RiotModelHandle handle, int subMeshIndex, const wchar_t* texturePath);

    // Advances the model's animation clock. Call once per frame before DrawRiotModel.
    RIOT_API void UpdateRiotAnimation(RiotModelHandle handle, float deltaTime);

    // Draws the champion at the given screen position, matching the same conventions as
    // Graphics::SceneRenderer::DrawMesh3D (x/y in screen space, angle in radians, scale as zoom factor).
    RIOT_API void DrawRiotModel(RiotModelHandle handle, float x, float y, float angle, float scale, float alpha = 1.0f);

    RIOT_API bool IsRiotModelValid(RiotModelHandle handle);
}

