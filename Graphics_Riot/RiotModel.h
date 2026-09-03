#pragma once
#include "RiotFormats.h"
#include "RiotTexture.h"
#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include <vector>
#include <string>

namespace Riot {

    // Runtime skinned vertex sent to the GPU (position already includes normal for lighting-lite).
    struct RiotGpuVertex {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT2 uv;
    };

    // Per-submesh runtime state (visibility toggle + optional texture override), mirroring the
    // reference LoL viewer's "Meshes" list where each object (body, weapon, prop, etc.) can be
    // shown/hidden and assigned its own texture found in the skin's folder.
    struct RiotSubMeshRuntime {
        std::string name;
        bool visible = true;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> overrideTextureSRV; // null = usa a textura padrao do modelo
    };

    class RiotModel {
    public:
        bool LoadFromFiles(ID3D11Device* device, const std::string& sknPath, const std::string& sklPath,
            const std::string& anmPath, const std::wstring& texturePath);

        // Advances animation time and recomputes CPU-skinned vertex buffer.
        void UpdateAnimation(ID3D11DeviceContext* context, float deltaTime);

        void Draw(ID3D11DeviceContext* context);

        bool IsValid() const { return valid; }

        int GetSubMeshCount() const { return (int)skn.subMeshes.size(); }
        const char* GetSubMeshName(int index) const {
            if (index < 0 || index >= (int)subMeshRuntime.size()) return "";
            return subMeshRuntime[index].name.c_str();
        }
        void SetSubMeshVisible(int index, bool visible) {
            if (index < 0 || index >= (int)subMeshRuntime.size()) return;
            subMeshRuntime[index].visible = visible;
        }
        bool IsSubMeshVisible(int index) const {
            if (index < 0 || index >= (int)subMeshRuntime.size()) return false;
            return subMeshRuntime[index].visible;
        }
        bool SetSubMeshTexture(ID3D11Device* device, int index, const std::wstring& texturePath) {
            if (index < 0 || index >= (int)subMeshRuntime.size()) return false;
            if (texturePath.empty()) {
                subMeshRuntime[index].overrideTextureSRV.Reset();
                return true;
            }
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            if (!RiotTexture::Load(device, texturePath, srv)) return false;
            subMeshRuntime[index].overrideTextureSRV = srv;
            return true;
        }

    private:
        void ComputeBoneMatricesAtTime(float time, std::vector<XMFLOAT4X4>& outMatrices) const;
        void RebuildSkinnedVertices(const std::vector<XMFLOAT4X4>& boneMatrices);
        bool CreateGpuResources(ID3D11Device* device);

        SknModel skn;
        SklModel skl;
        AnmModel anm;

        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textureSRV; // textura padrao (compartilhada)

        std::vector<RiotSubMeshRuntime> subMeshRuntime;

        std::vector<RiotGpuVertex> skinnedVertices;

        ID3D11Device* cachedDevice = nullptr;
        float animTime = 0.0f;
        bool valid = false;
    };
}

