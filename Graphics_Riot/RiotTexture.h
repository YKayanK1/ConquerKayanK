#pragma once
#include "framework.h"
#include <d3d11.h>
#include <wrl.h>
#include <string>

namespace Riot {

    class RiotTexture {
    public:
        // Loads a texture detecting format by file extension (.dds or .tex).
        // Returns true on success and fills outSRV.
        static bool Load(ID3D11Device* device, const std::wstring& filePath,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV);

    private:
        static bool LoadDDS(ID3D11Device* device, const std::wstring& filePath,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV);

        static bool LoadTex(ID3D11Device* device, const std::wstring& filePath,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV);
    };
}
