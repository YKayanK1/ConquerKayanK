#include "pch.h"
#include "RiotTexture.h"
#include <DDSTextureLoader.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>

namespace Riot {

    namespace {
        std::wstring ToLowerExt(const std::wstring& path) {
            size_t dot = path.find_last_of(L'.');
            if (dot == std::wstring::npos) return L"";
            std::wstring ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
            return ext;
        }

        template<typename T>
        bool ReadRaw(std::ifstream& f, T& out) {
            f.read(reinterpret_cast<char*>(&out), sizeof(T));
            return (bool)f;
        }

        // Riot .tex header (formato real, confirmado com o parser de referencia em Rust):
        // char magic[4]   = "TEX\0"
        // uint16 width
        // uint16 height
        // uint8  unknown0   (nao usado)
        // uint8  format     (1=ETC1, 2=ETC2EAC, 10|11=BC1/DXT1, 12=BC3/DXT5, 20=RGBA8)
        // uint8  unknown1   (nao usado)
        // uint8  hasMipmaps (0 = sem mipmaps, != 0 = com mipmaps)
#pragma pack(push, 1)
        struct TexHeader {
            char magic[4];
            uint16_t width;
            uint16_t height;
            uint8_t unknown0;
            uint8_t format;
            uint8_t unknown1;
            uint8_t hasMipmaps;
        };
#pragma pack(pop)

        DXGI_FORMAT MapTexFormat(uint8_t format, bool& outSupported) {
            outSupported = true;
            switch (format) {
            case 10:
            case 11: return DXGI_FORMAT_BC1_UNORM;
            case 12: return DXGI_FORMAT_BC3_UNORM;
            case 20: return DXGI_FORMAT_R8G8B8A8_UNORM;
            default:
                // ETC1 (1) e ETC2EAC (2) nao sao suportados nativamente pelo D3D11 desktop.
                outSupported = false;
                return DXGI_FORMAT_BC1_UNORM;
            }
        }

        size_t BlockSizeForFormat(DXGI_FORMAT fmt) {
            switch (fmt) {
            case DXGI_FORMAT_BC1_UNORM: return 8;
            case DXGI_FORMAT_BC3_UNORM: return 16;
            case DXGI_FORMAT_BC7_UNORM: return 16;
            default: return 0; // uncompressed handled separately
            }
        }
    }

    bool RiotTexture::Load(ID3D11Device* device, const std::wstring& filePath,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV) {

        std::wstring ext = ToLowerExt(filePath);
        if (ext == L".dds") {
            return LoadDDS(device, filePath, outSRV);
        } else if (ext == L".tex") {
            return LoadTex(device, filePath, outSRV);
        }
        return false;
    }

    bool RiotTexture::LoadDDS(ID3D11Device* device, const std::wstring& filePath,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV) {
        HRESULT hr = DirectX::CreateDDSTextureFromFile(device, filePath.c_str(), nullptr, outSRV.GetAddressOf());
        return SUCCEEDED(hr);
    }

    bool RiotTexture::LoadTex(ID3D11Device* device, const std::wstring& filePath,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV) {

        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return false;

        TexHeader header{};
        if (!ReadRaw(f, header)) return false;
        if (std::memcmp(header.magic, "TEX", 3) != 0) return false;

        bool formatSupported = true;
        DXGI_FORMAT dxgiFormat = MapTexFormat(header.format, formatSupported);
        if (!formatSupported) return false; // ETC1/ETC2 nao suportados no D3D11 desktop.
        size_t blockSize = BlockSizeForFormat(dxgiFormat);

        uint32_t width = header.width;
        uint32_t heightC = header.height;

        // Quantidade de niveis de mipmap: min(32, floor(log2(max(w,h))) + 1), igual ao parser de referencia.
        size_t mipCount = 1;
        if (header.hasMipmaps != 0) {
            uint32_t maxDim = std::max<uint32_t>(width, heightC);
            uint32_t levels = 1;
            while ((maxDim >> levels) >= 1) ++levels;
            mipCount = std::min<size_t>(32, levels);
        }

        std::vector<UINT> mipWidths(mipCount), mipHeights(mipCount);
        {
            uint32_t w = width, h = heightC;
            for (size_t i = 0; i < mipCount; ++i) {
                mipWidths[i] = std::max<uint32_t>(1, w);
                mipHeights[i] = std::max<uint32_t>(1, h);
                w /= 2; h /= 2;
            }
        }

        // O restante do arquivo (apos o header) contem todos os niveis de mipmap concatenados,
        // porem gravados do MAIOR nivel para o MENOR (ordem normal), mas o parser de referencia
        // extrai cada bloco a partir do FINAL do buffer, decrementando o offset, comecando pelo
        // nivel 0 (tamanho completo) e avancando para os niveis menores. Ou seja, os dados no
        // arquivo ficam armazenados com o nivel 0 no final do buffer e os niveis menores antes dele.
        std::vector<uint8_t> allData((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        auto blockSizeForDim = [&](uint32_t w, uint32_t h) -> size_t {
            if (blockSize > 0) {
                uint32_t blocksWide = std::max<uint32_t>(1, (w + 3) / 4);
                uint32_t blocksHigh = std::max<uint32_t>(1, (h + 3) / 4);
                return (size_t)blocksWide * blockSize * blocksHigh;
            }
            return (size_t)w * 4 * h;
            };

        std::vector<std::vector<uint8_t>> mipData(mipCount);
        size_t offset = allData.size();
        for (size_t i = 0; i < mipCount; ++i) {
            size_t sliceBytes = blockSizeForDim(mipWidths[i], mipHeights[i]);
            if (sliceBytes > offset) return false;
            offset -= sliceBytes;
            mipData[i].assign(allData.begin() + offset, allData.begin() + offset + sliceBytes);
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = heightC;
        desc.MipLevels = (UINT)mipCount;
        desc.ArraySize = 1;
        desc.Format = dxgiFormat;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> subresources(mipCount);
        for (size_t i = 0; i < mipCount; ++i) {
            uint32_t w = mipWidths[i];
            size_t rowBytes = blockSize > 0
                ? (size_t)std::max<uint32_t>(1, (w + 3) / 4) * blockSize
                : (size_t)w * 4;
            subresources[i].pSysMem = mipData[i].data();
            subresources[i].SysMemPitch = (UINT)rowBytes;
            subresources[i].SysMemSlicePitch = 0;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = device->CreateTexture2D(&desc, subresources.data(), &texture);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        hr = device->CreateShaderResourceView(texture.Get(), &srvDesc, outSRV.GetAddressOf());
        return SUCCEEDED(hr);
    }
}
