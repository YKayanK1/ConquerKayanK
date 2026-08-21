#include "pch.h"
#include "Graphics.h"
#include "Graphics_D3D.h"

#include <d3d11.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>

#include <DDSTextureLoader.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// A Ponte Indestrutível do Linker
float g_UI_U1 = 0.0f;
float g_UI_V1 = 0.0f;
float g_UI_U2 = 1.0f;
float g_UI_V2 = 1.0f;

extern "C" __declspec(dllexport) void SetSpriteUV(float u1, float v1, float u2, float v2) {
    g_UI_U1 = u1;
    g_UI_V1 = v1;
    g_UI_U2 = u2;
    g_UI_V2 = v2;
}

namespace Graphics {

    const char* vertexShader2DCode = R"(
        cbuffer ConstantBuffer : register(b0) { matrix WVP; matrix Bones[128]; int HasAnimation; float Alpha; float TimeFrame; int UVMode; float4 UVRect; }
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
        VOut main(float3 pos : POSITION, float2 tex : TEXCOORD) {
            VOut output;
            output.position = mul(float4(pos, 1.0f), WVP);
            output.tex.x = lerp(UVRect.x, UVRect.z, tex.x);
            output.tex.y = lerp(UVRect.y, UVRect.w, tex.y);
            return output;
        }
    )";

    const char* pixelShader2DCode = R"(
        Texture2D shaderTexture : register(t0); SamplerState sampleType : register(s0);
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
        float4 main(VOut input) : SV_TARGET {
            float4 color = shaderTexture.Sample(sampleType, input.tex);
            clip(color.a - 0.1f); return color;
        }
    )";

    const char* vertexShader3DCode = R"(
        cbuffer ConstantBuffer : register(b0) { matrix WVP; matrix Bones[128]; int HasAnimation; float Alpha; float TimeFrame; int UVMode; float4 UVRect; }
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; float alpha : COLOR; };
        
        VOut main(float3 pos : POSITION, float2 tex : TEXCOORD, uint2 boneIdx : BLENDINDICES, float2 boneWt : BLENDWEIGHT) {
            VOut output;
            
            // [RESTAURADO DA SUA VERSÃO OLD] Resolve o Tornado Gigante e mantém a Boneca inteira!
            if (HasAnimation == 1) {
                if (boneWt.x > 0.0f) output.position = mul(float4(pos, 1.0f), Bones[boneIdx.x]);
                else if (boneWt.y > 0.0f) output.position = mul(float4(pos, 1.0f), Bones[boneIdx.y]);
                else output.position = mul(float4(pos, 1.0f), WVP);
            } else {
                output.position = mul(float4(pos, 1.0f), WVP);
            }
            
            output.tex = tex;
            
            // Fatiamento 4x4 Ativado apenas para o casaco/raio do mago
            if (UVMode == 1) {
                int totalFrames = 16;
                int frameIdx = ((int)(TimeFrame) / 2) % totalFrames; 
                float col = frameIdx % 4;
                float row = frameIdx / 4;
                output.tex.x = (output.tex.x * 0.25f) + (col * 0.25f);
                output.tex.y = (output.tex.y * 0.25f) + (row * 0.25f);
            } 
            
            output.alpha = Alpha; 
            return output;
        }
    )";

    const char* pixelShader3DCode = R"(
        Texture2D shaderTexture : register(t0); SamplerState sampleType : register(s0);
        struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; float alpha : COLOR; };
        float4 main(VOut input) : SV_TARGET {
            float4 color = shaderTexture.Sample(sampleType, input.tex);
            if (color.r < 0.05f && color.g < 0.05f && color.b < 0.05f) discard;
            clip(color.a - 0.05f); 
            return float4(color.rgb, color.a * input.alpha); 
        }
    )";

    const char* vertexShaderPtclCode = R"(
        cbuffer ConstantBuffer : register(b0) { matrix WVP; matrix Bones[128]; int HasAnimation; float Alpha; float TimeFrame; int UVMode; float4 UVRect; }
        struct VOut { float4 position : SV_POSITION; float4 color : COLOR; float2 tex : TEXCOORD; };
        VOut main(float3 pos : POSITION, float4 color : COLOR, float2 tex : TEXCOORD) {
            VOut output;
            output.position = mul(float4(pos, 1.0f), WVP); output.color = color; output.tex = tex;
            return output;
        }
    )";

    const char* pixelShaderPtclCode = R"(
        Texture2D shaderTexture : register(t0); SamplerState sampleType : register(s0);
        struct VOut { float4 position : SV_POSITION; float4 color : COLOR; float2 tex : TEXCOORD; };
        float4 main(VOut input) : SV_TARGET {
            float4 texColor = shaderTexture.Sample(sampleType, input.tex);
            if (texColor.r < 0.05f && texColor.g < 0.05f && texColor.b < 0.05f) discard;
            return texColor * input.color; 
        }
    )";

    struct Vertex2D { DirectX::XMFLOAT3 Pos; DirectX::XMFLOAT2 Tex; };
    struct ConstantBuffer { DirectX::XMMATRIX WVP; DirectX::XMMATRIX Bones[128]; int HasAnimation; float Alpha; float TimeFrame; int UVMode; DirectX::XMFLOAT4 UVRect; };
    struct VertexPtcl { DirectX::XMFLOAT3 Pos; DirectX::XMFLOAT4 Color; DirectX::XMFLOAT2 Tex; };

    struct MeshKey {
        uint32_t vCount;
        uint32_t iCount;
        float px, py, pz;
        bool operator==(const MeshKey& o) const {
            return vCount == o.vCount && iCount == o.iCount && px == o.px && py == o.py && pz == o.pz;
        }
    };
    struct MeshKeyHash {
        std::size_t operator()(const MeshKey& k) const {
            return std::hash<uint32_t>()(k.vCount) ^ (std::hash<uint32_t>()(k.iCount) << 1) ^ (std::hash<float>()(k.px) << 2);
        }
    };

    class MasterRenderer {
    public:
        Microsoft::WRL::ComPtr<ID3D11Buffer> vb2D;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs2D, vs3D, vsPtcl;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ps2D, ps3D, psPtcl;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> layout2D, layout3D, layoutPtcl;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState3D;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;

        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState2D;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState3D;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStateNoWrite;

        Microsoft::WRL::ComPtr<ID3D11BlendState> blendStateAlpha;
        Microsoft::WRL::ComPtr<ID3D11BlendState> blendStateAdditive;

        Microsoft::WRL::ComPtr<ID3D11Buffer> ptclVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer> ptclIB;

        std::unordered_map<int, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textures;
        int nextTextureId = 1;

        struct MeshCache {
            Microsoft::WRL::ComPtr<ID3D11Buffer> vb;
            Microsoft::WRL::ComPtr<ID3D11Buffer> ib;
            UINT indexCount;
        };
        std::unordered_map<MeshKey, MeshCache, MeshKeyHash> m_meshCache;

        void Initialize() {
            auto& d3d = D3DContext::GetInstance();
            auto device = d3d.device;

            Microsoft::WRL::ComPtr<ID3DBlob> vsBlob2, psBlob2, vsBlob3, psBlob3, vsBlobPtcl, psBlobPtcl;
            D3DCompile(vertexShader2DCode, strlen(vertexShader2DCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob2, nullptr);
            D3DCompile(pixelShader2DCode, strlen(pixelShader2DCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob2, nullptr);
            D3DCompile(vertexShader3DCode, strlen(vertexShader3DCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob3, nullptr);
            D3DCompile(pixelShader3DCode, strlen(pixelShader3DCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob3, nullptr);
            D3DCompile(vertexShaderPtclCode, strlen(vertexShaderPtclCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlobPtcl, nullptr);
            D3DCompile(pixelShaderPtclCode, strlen(pixelShaderPtclCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlobPtcl, nullptr);

            device->CreateVertexShader(vsBlob2->GetBufferPointer(), vsBlob2->GetBufferSize(), nullptr, &vs2D);
            device->CreatePixelShader(psBlob2->GetBufferPointer(), psBlob2->GetBufferSize(), nullptr, &ps2D);
            device->CreateVertexShader(vsBlob3->GetBufferPointer(), vsBlob3->GetBufferSize(), nullptr, &vs3D);
            device->CreatePixelShader(psBlob3->GetBufferPointer(), psBlob3->GetBufferSize(), nullptr, &ps3D);
            device->CreateVertexShader(vsBlobPtcl->GetBufferPointer(), vsBlobPtcl->GetBufferSize(), nullptr, &vsPtcl);
            device->CreatePixelShader(psBlobPtcl->GetBufferPointer(), psBlobPtcl->GetBufferSize(), nullptr, &psPtcl);

            D3D11_INPUT_ELEMENT_DESC lay2D[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };
            device->CreateInputLayout(lay2D, 2, vsBlob2->GetBufferPointer(), vsBlob2->GetBufferSize(), &layout2D);

            D3D11_INPUT_ELEMENT_DESC lay3D[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "BLENDINDICES", 0, DXGI_FORMAT_R32G32_UINT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };
            device->CreateInputLayout(lay3D, 4, vsBlob3->GetBufferPointer(), vsBlob3->GetBufferSize(), &layout3D);

            D3D11_INPUT_ELEMENT_DESC layPtcl[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };
            device->CreateInputLayout(layPtcl, 3, vsBlobPtcl->GetBufferPointer(), vsBlobPtcl->GetBufferSize(), &layoutPtcl);

            Vertex2D vertices[] = {
                { DirectX::XMFLOAT3(0.0f, 0.0f, 0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
                { DirectX::XMFLOAT3(1.0f, 0.0f, 0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
                { DirectX::XMFLOAT3(0.0f, 1.0f, 0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
                { DirectX::XMFLOAT3(1.0f, 1.0f, 0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) }
            };
            D3D11_BUFFER_DESC bd = {};
            bd.Usage = D3D11_USAGE_DEFAULT; bd.ByteWidth = sizeof(Vertex2D) * 4; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initData = {}; initData.pSysMem = vertices;
            device->CreateBuffer(&bd, &initData, &vb2D);

            bd.ByteWidth = sizeof(ConstantBuffer); bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            device->CreateBuffer(&bd, nullptr, &constantBuffer);

            D3D11_SAMPLER_DESC sampDesc = {};
            sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP; sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP; sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            device->CreateSamplerState(&sampDesc, &samplerState);

            D3D11_RASTERIZER_DESC rastDesc = {};
            rastDesc.FillMode = D3D11_FILL_SOLID;
            rastDesc.CullMode = D3D11_CULL_NONE;
            device->CreateRasterizerState(&rastDesc, &rasterState3D);

            D3D11_DEPTH_STENCIL_DESC dsDesc2D = {};
            dsDesc2D.DepthEnable = FALSE;
            dsDesc2D.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            device->CreateDepthStencilState(&dsDesc2D, &depthState2D);

            D3D11_DEPTH_STENCIL_DESC dsDesc3D = {};
            dsDesc3D.DepthEnable = TRUE;
            dsDesc3D.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsDesc3D.DepthFunc = D3D11_COMPARISON_LESS;
            device->CreateDepthStencilState(&dsDesc3D, &depthState3D);

            D3D11_DEPTH_STENCIL_DESC dsDescNW = {};
            dsDescNW.DepthEnable = FALSE;
            dsDescNW.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDescNW.DepthFunc = D3D11_COMPARISON_ALWAYS;
            device->CreateDepthStencilState(&dsDescNW, &depthStateNoWrite);

            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            device->CreateBlendState(&blendDesc, &blendStateAlpha);

            D3D11_BLEND_DESC addDesc = {};
            addDesc.RenderTarget[0].BlendEnable = TRUE;
            addDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            addDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            addDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            addDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
            addDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            addDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            addDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            device->CreateBlendState(&addDesc, &blendStateAdditive);

            D3D11_BUFFER_DESC pbd = {};
            pbd.Usage = D3D11_USAGE_DYNAMIC;
            pbd.ByteWidth = sizeof(VertexPtcl) * 4000 * 4;
            pbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            pbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            device->CreateBuffer(&pbd, nullptr, &ptclVB);

            std::vector<uint16_t> ptclIndices(4000 * 6);
            for (int i = 0; i < 4000; i++) {
                ptclIndices[i * 6 + 0] = i * 4 + 0; ptclIndices[i * 6 + 1] = i * 4 + 1; ptclIndices[i * 6 + 2] = i * 4 + 2;
                ptclIndices[i * 6 + 3] = i * 4 + 2; ptclIndices[i * 6 + 4] = i * 4 + 1; ptclIndices[i * 6 + 5] = i * 4 + 3;
            }
            D3D11_BUFFER_DESC pib = {};
            pib.Usage = D3D11_USAGE_DEFAULT; pib.ByteWidth = sizeof(uint16_t) * ptclIndices.size();
            pib.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA pinit = {}; pinit.pSysMem = ptclIndices.data();
            device->CreateBuffer(&pib, &pinit, &ptclIB);

            d3d.context->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
        }

        int LoadTextureFromMemory(const uint8_t* data, size_t size) {
            if (!data || size == 0) return -1;

            if (size > 18 && data[2] == 2) {
                int w = data[12] | (data[13] << 8);
                int h = data[14] | (data[15] << 8);
                int bpp = data[16];

                if ((bpp == 24 || bpp == 32) && w > 0 && h > 0 && w < 4096 && h < 4096) {
                    std::vector<uint8_t> rgba(w * h * 4, 255);
                    int offset = 18;
                    bool valid = true;
                    for (int i = 0; i < w * h; i++) {
                        if (offset + (bpp / 8) > size) { valid = false; break; }
                        rgba[i * 4 + 2] = data[offset++];
                        rgba[i * 4 + 1] = data[offset++];
                        rgba[i * 4 + 0] = data[offset++];
                        if (bpp == 32) rgba[i * 4 + 3] = data[offset++];
                    }
                    if (valid) {
                        D3D11_TEXTURE2D_DESC desc = {};
                        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
                        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
                        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                        D3D11_SUBRESOURCE_DATA initData = {};
                        initData.pSysMem = rgba.data(); initData.SysMemPitch = w * 4;
                        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                        if (SUCCEEDED(D3DContext::GetInstance().device->CreateTexture2D(&desc, &initData, &tex))) {
                            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
                            if (SUCCEEDED(D3DContext::GetInstance().device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
                                int id = nextTextureId++; textures[id] = srv; return id;
                            }
                        }
                    }
                }
            }

            if (size > 54 && data[0] == 'B' && data[1] == 'M') {
                int dataOffset = data[10] | (data[11] << 8) | (data[12] << 16) | (data[13] << 24);
                int w = data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24);
                int h = data[22] | (data[23] << 8) | (data[24] << 16) | (data[25] << 24);
                int bpp = data[28] | (data[29] << 8);

                if ((bpp == 24 || bpp == 32) && w > 0 && h > 0 && w < 4096 && h < 4096) {
                    std::vector<uint8_t> rgba(w * h * 4, 255);
                    int rowBytes = ((w * bpp + 31) / 32) * 4;
                    bool valid = true;
                    for (int y = 0; y < h; y++) {
                        int srcY = h - 1 - y;
                        int offset = dataOffset + srcY * rowBytes;
                        for (int x = 0; x < w; x++) {
                            if (offset + (bpp / 8) > size) { valid = false; break; }
                            rgba[(y * w + x) * 4 + 2] = data[offset++];
                            rgba[(y * w + x) * 4 + 1] = data[offset++];
                            rgba[(y * w + x) * 4 + 0] = data[offset++];
                            if (bpp == 32) rgba[(y * w + x) * 4 + 3] = data[offset++];
                        }
                    }
                    if (valid) {
                        D3D11_TEXTURE2D_DESC desc = {};
                        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
                        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
                        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                        D3D11_SUBRESOURCE_DATA initData = {};
                        initData.pSysMem = rgba.data(); initData.SysMemPitch = w * 4;
                        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                        if (SUCCEEDED(D3DContext::GetInstance().device->CreateTexture2D(&desc, &initData, &tex))) {
                            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
                            if (SUCCEEDED(D3DContext::GetInstance().device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
                                int id = nextTextureId++; textures[id] = srv; return id;
                            }
                        }
                    }
                }
            }

            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newTexture;
            HRESULT hr = DirectX::CreateDDSTextureFromMemory(D3DContext::GetInstance().device.Get(), data, size, nullptr, newTexture.GetAddressOf());
            if (FAILED(hr)) return -1;
            int id = nextTextureId++; textures[id] = newTexture; return id;
        }

        void DeleteTexture(int id) {
            if (textures.count(id)) {
                textures.erase(id);
            }
        }

        void ApplyBlendState(int adb, int colorEnable) {
            auto ctx = D3DContext::GetInstance().context;
            if (adb == 2 || adb == 4 || colorEnable == 1) {
                ctx->OMSetBlendState(blendStateAdditive.Get(), nullptr, 0xFFFFFFFF);
            }
            else {
                ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
            }
        }

        void DrawSprite(int textureId, int x, int y, int width, int height) {
            auto& d3d = D3DContext::GetInstance(); auto ctx = d3d.context;

            ctx->OMSetDepthStencilState(depthState2D.Get(), 0);

            DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(0.0f, (float)d3d.screenWidth, (float)d3d.screenHeight, 0.0f, 0.0f, 1.0f);
            DirectX::XMMATRIX world = DirectX::XMMatrixScaling((float)width, (float)height, 1.0f) * DirectX::XMMatrixTranslation((float)x, (float)y, 0.0f);

            ConstantBuffer cb;
            cb.WVP = DirectX::XMMatrixTranspose(world * ortho);
            cb.HasAnimation = 0;
            cb.UVRect = DirectX::XMFLOAT4(g_UI_U1, g_UI_V1, g_UI_U2, g_UI_V2);
            g_UI_U1 = 0.0f; g_UI_V1 = 0.0f; g_UI_U2 = 1.0f; g_UI_V2 = 1.0f;

            ctx->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

            UINT stride = sizeof(Vertex2D); UINT offset = 0;
            ctx->IASetInputLayout(layout2D.Get());
            ctx->IASetVertexBuffers(0, 1, vb2D.GetAddressOf(), &stride, &offset);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ctx->VSSetShader(vs2D.Get(), nullptr, 0); ctx->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
            ctx->PSSetShader(ps2D.Get(), nullptr, 0);
            if (textures.count(textureId)) { ctx->PSSetShaderResources(0, 1, textures[textureId].GetAddressOf()); ctx->PSSetSamplers(0, 1, samplerState.GetAddressOf()); }
            ctx->Draw(4, 0);
        }

        static DirectX::XMMATRIX LerpMatrix(DirectX::XMMATRIX a, DirectX::XMMATRIX b, float t) {
            DirectX::XMVECTOR row0 = DirectX::XMVectorLerp(a.r[0], b.r[0], t);
            DirectX::XMVECTOR row1 = DirectX::XMVectorLerp(a.r[1], b.r[1], t);
            DirectX::XMVECTOR row2 = DirectX::XMVectorLerp(a.r[2], b.r[2], t);
            DirectX::XMVECTOR row3 = DirectX::XMVectorLerp(a.r[3], b.r[3], t);
            return DirectX::XMMATRIX(row0, row1, row2, row3);
        }

        DirectX::XMMATRIX GetInterpolatedBone(const Resource::C3Motion& motion, int boneIdx, int frame) {
            if (motion.keyframes.empty() || boneIdx < 0 || boneIdx >= motion.boneCount)
                return DirectX::XMMatrixIdentity();

            int totalFrames = motion.frameCount > 0 ? motion.frameCount : 1;
            int currentFrame = frame % totalFrames;

            int s = -1, e = -1;
            for (size_t n = 0; n < motion.keyframes.size(); n++) {
                if (motion.keyframes[n].pos <= currentFrame) { if (s == -1 || (int)n > s) s = (int)n; }
                if (motion.keyframes[n].pos > currentFrame) { if (e == -1 || (int)n < e) e = (int)n; }
            }
            if (s == -1 && e > -1) return DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)motion.keyframes[e].boneMatrices[boneIdx].m);
            if (s > -1 && e == -1) return DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)motion.keyframes[s].boneMatrices[boneIdx].m);

            if (s > -1 && e > -1) {
                DirectX::XMMATRIX matS = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)motion.keyframes[s].boneMatrices[boneIdx].m);
                DirectX::XMMATRIX matE = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)motion.keyframes[e].boneMatrices[boneIdx].m);
                if (s == e) return matS;
                float t = (float)(currentFrame - motion.keyframes[s].pos) / (float)(motion.keyframes[e].pos - motion.keyframes[s].pos);

                return LerpMatrix(matS, matE, t);
            }
            return DirectX::XMMatrixIdentity();
        }

        void DrawMesh3D(const Resource::C3Model& model, float x, float y, int textureId, int frame, float angle, float pitch, bool isPlayer, float scale, const Resource::C3Model* parentModel, int linkBoneIndex, int parentFrame, int asb, int adb, float alpha, bool disableZWrite, int colorEnable) {
            if (model.phys.empty()) return;

            auto& d3d = D3DContext::GetInstance();
            auto ctx = d3d.context;

            if (disableZWrite) {
                ctx->OMSetDepthStencilState(depthStateNoWrite.Get(), 0);
            }
            else {
                ctx->OMSetDepthStencilState(depthState3D.Get(), 0);
            }

            int actualColorEnable = colorEnable;
            if (colorEnable >= 100) {
                actualColorEnable = colorEnable % 100;
            }

            ApplyBlendState(adb, actualColorEnable);

            DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(
                0.0f, (float)d3d.screenWidth, (float)d3d.screenHeight, 0.0f, -1000.0f, 1000.0f
            );

            DirectX::XMMATRIX rotZ = DirectX::XMMatrixRotationZ(angle);
            DirectX::XMMATRIX rotX = DirectX::XMMatrixRotationX(1.04719f);

            float s = 0.6f * scale;
            DirectX::XMMATRIX modelScale = DirectX::XMMatrixScaling(s, -s, s);

            DirectX::XMMATRIX attachmentMat = DirectX::XMMatrixIdentity();
            if (parentModel != nullptr && linkBoneIndex >= 0 && linkBoneIndex < (int)parentModel->motions.size()) {
                if (parentModel->motions[linkBoneIndex].boneCount > 0) {
                    attachmentMat = GetInterpolatedBone(parentModel->motions[linkBoneIndex], 0, parentFrame);
                }
            }

            DirectX::XMMATRIX localPitch = DirectX::XMMatrixRotationX(pitch);
            DirectX::XMMATRIX world = localPitch * attachmentMat * rotZ * rotX * modelScale * DirectX::XMMatrixTranslation(x, y, 0.0f);

            ConstantBuffer cb;
            cb.HasAnimation = 1;
            cb.Alpha = alpha;
            cb.TimeFrame = (float)frame;
            cb.UVRect = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);

            ctx->IASetInputLayout(layout3D.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(vs3D.Get(), nullptr, 0);
            ctx->PSSetShader(ps3D.Get(), nullptr, 0);
            ctx->RSSetState(rasterState3D.Get());

            if (textures.count(textureId)) {
                ctx->PSSetShaderResources(0, 1, textures[textureId].GetAddressOf());
                ctx->PSSetSamplers(0, 1, samplerState.GetAddressOf());
            }
            else {
                ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
                ctx->PSSetShaderResources(0, 1, nullSRV);
            }

            for (size_t i = 0; i < model.phys.size(); i++) {
                const auto& phy = model.phys[i];

                std::string nameLower = phy.name;
                for (auto& c : nameLower) c = std::tolower(c);

                if (isPlayer) {
                    if (nameLower.find("body") == std::string::npos) continue;
                }
                else {
                    if (nameLower.find("dummy") != std::string::npos || nameLower.find("point") != std::string::npos) continue;
                }

                cb.UVMode = 0;
                if (colorEnable >= 100) {
                    if (nameLower.find("coat") != std::string::npos) {
                        cb.UVMode = 1;
                    }
                }

                DirectX::XMMATRIX initMat = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)phy.initMatrix.m);
                cb.WVP = DirectX::XMMatrixTranspose(initMat * world * ortho);

                cb.HasAnimation = 0;
                for (int b = 0; b < 128; b++) {
                    cb.Bones[b] = DirectX::XMMatrixTranspose(DirectX::XMMatrixIdentity());
                }

                if (i < model.motions.size()) {
                    const auto& motion = model.motions[i];
                    if (motion.boneCount > 0) {
                        cb.HasAnimation = 1;
                        for (int b = 0; b < motion.boneCount && b < 128; b++) {
                            DirectX::XMMATRIX keyMat = GetInterpolatedBone(motion, b, frame);
                            DirectX::XMMATRIX finalBoneMat = initMat * keyMat * world * ortho;
                            cb.Bones[b] = DirectX::XMMatrixTranspose(finalBoneMat);
                        }
                    }
                }

                ctx->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
                ctx->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());

                MeshKey key = { (uint32_t)phy.vertices.size(), (uint32_t)phy.indices.size(), 0.0f, 0.0f, 0.0f };
                if (!phy.vertices.empty()) {
                    key.px = phy.vertices[0].px;
                    key.py = phy.vertices[0].py;
                    key.pz = phy.vertices[0].pz;
                }

                if (m_meshCache.find(key) == m_meshCache.end()) {
                    MeshCache cache;
                    cache.indexCount = (UINT)phy.indices.size();

                    D3D11_BUFFER_DESC vbd = {};
                    vbd.Usage = D3D11_USAGE_DEFAULT;
                    vbd.ByteWidth = sizeof(Resource::PhyVertex) * (UINT)phy.vertices.size();
                    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    D3D11_SUBRESOURCE_DATA vinit = {}; vinit.pSysMem = phy.vertices.data();
                    d3d.device->CreateBuffer(&vbd, &vinit, &cache.vb);

                    D3D11_BUFFER_DESC ibd = {};
                    ibd.Usage = D3D11_USAGE_DEFAULT;
                    ibd.ByteWidth = sizeof(uint16_t) * cache.indexCount;
                    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
                    D3D11_SUBRESOURCE_DATA iinit = {}; iinit.pSysMem = phy.indices.data();
                    d3d.device->CreateBuffer(&ibd, &iinit, &cache.ib);

                    m_meshCache[key] = cache;
                }

                auto& cache = m_meshCache[key];
                UINT stride = sizeof(Resource::PhyVertex);
                UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, cache.vb.GetAddressOf(), &stride, &offset);
                ctx->IASetIndexBuffer(cache.ib.Get(), DXGI_FORMAT_R16_UINT, 0);

                ctx->DrawIndexed(cache.indexCount, 0, 0);
            }

            ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
        }

        void DrawParticles(const Resource::C3Model& model, float x, float y, int textureId, int frame, float angle, float pitch, float scale, int asb, int adb, const Resource::C3Model* parentModel, int linkBoneIndex, int parentFrame, int colorEnable) {
            if (model.ptcls.empty()) return;

            auto& d3d = D3DContext::GetInstance();
            auto ctx = d3d.context;

            ctx->OMSetDepthStencilState(depthState2D.Get(), 0);

            int actualColorEnable = colorEnable % 100;
            ApplyBlendState(adb, actualColorEnable);

            DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(
                0.0f, (float)d3d.screenWidth, (float)d3d.screenHeight, 0.0f, -1000.0f, 1000.0f
            );

            DirectX::XMMATRIX localPitch = DirectX::XMMatrixRotationX(pitch);
            DirectX::XMMATRIX rotZ = DirectX::XMMatrixRotationZ(angle);
            DirectX::XMMATRIX rotX = DirectX::XMMatrixRotationX(1.04719f);

            float s = 0.6f * scale;
            DirectX::XMMATRIX modelScale = DirectX::XMMatrixScaling(s, -s, s);

            DirectX::XMMATRIX attachmentMat = DirectX::XMMatrixIdentity();
            if (parentModel != nullptr && linkBoneIndex >= 0 && linkBoneIndex < (int)parentModel->motions.size()) {
                if (parentModel->motions[linkBoneIndex].boneCount > 0) {
                    attachmentMat = GetInterpolatedBone(parentModel->motions[linkBoneIndex], 0, parentFrame);
                }
            }

            DirectX::XMMATRIX world = localPitch * attachmentMat * rotZ * rotX * modelScale * DirectX::XMMatrixTranslation(x, y, 0.0f);

            ConstantBuffer cb;
            cb.WVP = DirectX::XMMatrixTranspose(ortho);
            cb.UVRect = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);
            ctx->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

            ctx->IASetInputLayout(layoutPtcl.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(vsPtcl.Get(), nullptr, 0);
            ctx->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
            ctx->PSSetShader(psPtcl.Get(), nullptr, 0);

            if (textures.count(textureId)) {
                ctx->PSSetShaderResources(0, 1, textures[textureId].GetAddressOf());
                ctx->PSSetSamplers(0, 1, samplerState.GetAddressOf());
            }

            D3D11_MAPPED_SUBRESOURCE mappedData;
            ctx->Map(ptclVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData);
            VertexPtcl* vertices = (VertexPtcl*)mappedData.pData;
            int vCount = 0;
            int quadCount = 0;

            for (const auto& ptcl : model.ptcls) {
                if (ptcl.frames.empty()) continue;
                int frameIdx = frame % ptcl.frames.size();
                const auto& f = ptcl.frames[frameIdx];

                DirectX::XMMATRIX frameMat = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)f.frameMatrix.m);
                DirectX::XMMATRIX xform = frameMat * world;

                float texRow = (float)ptcl.texRow;
                if (texRow < 1.0f) texRow = 1.0f;
                int segCount = ptcl.texRow * ptcl.texRow;
                float segSize = 1.0f / texRow;

                for (size_t i = 0; i < f.positions.size() && quadCount < 3999; i++) {
                    float age = f.ages[i];
                    float pSize = f.sizes[i] * s;

                    int tileIdx = (int)(age * segCount);
                    if (tileIdx < 0) tileIdx = 0;
                    if (tileIdx >= segCount) tileIdx = segCount - 1;
                    float u = (tileIdx % ptcl.texRow) * segSize;
                    float v = (tileIdx / ptcl.texRow) * segSize;

                    DirectX::XMFLOAT3 pos3; pos3.x = f.positions[i].x; pos3.y = f.positions[i].y; pos3.z = f.positions[i].z;
                    DirectX::XMVECTOR pVec = DirectX::XMLoadFloat3(&pos3);
                    pVec = DirectX::XMVector3Transform(pVec, xform);
                    DirectX::XMFLOAT3 vpos; DirectX::XMStoreFloat3(&vpos, pVec);

                    float alpha = ptcl.maxAlpha;
                    if (ptcl.isPTC3) {
                        if (age >= ptcl.fadeStartAge) {
                            if (age >= ptcl.fadeEndAge) {
                                float range = ptcl.totalLifetime - ptcl.fadeEndAge;
                                alpha = range > 0.0f ? (1.0f - (age - ptcl.fadeEndAge) / range) * ptcl.maxAlpha + ((age - ptcl.fadeEndAge) / range) * ptcl.minAlpha : ptcl.minAlpha;
                            }
                        }
                        else {
                            alpha = ptcl.fadeStartAge > 0.0f ? (1.0f - age / ptcl.fadeStartAge) * ptcl.initialAlpha + (age / ptcl.fadeStartAge) * ptcl.maxAlpha : ptcl.maxAlpha;
                        }
                    }
                    alpha *= ptcl.globalAlpha;
                    if (alpha < 0.0f) alpha = 0.0f; if (alpha > 1.0f) alpha = 1.0f;

                    DirectX::XMFLOAT4 color = { 1.0f * alpha, 1.0f * alpha, 1.0f * alpha, alpha };

                    float rx = pSize * ptcl.scaleX; float ry = pSize * ptcl.scaleY;
                    float ptclAngle = age * ptcl.rotationSpeed;
                    float cosRot = std::cos(ptclAngle); float sinRot = std::sin(ptclAngle);
                    float rightX = rx * cosRot; float rightY = rx * sinRot;
                    float upX = -ry * sinRot; float upY = ry * cosRot;

                    vertices[vCount++] = { DirectX::XMFLOAT3(vpos.x - rightX - upX, vpos.y - rightY - upY, vpos.z), color, DirectX::XMFLOAT2(u, v + segSize) };
                    vertices[vCount++] = { DirectX::XMFLOAT3(vpos.x + rightX - upX, vpos.y + rightY - upY, vpos.z), color, DirectX::XMFLOAT2(u + segSize, v + segSize) };
                    vertices[vCount++] = { DirectX::XMFLOAT3(vpos.x - rightX + upX, vpos.y - rightY + upY, vpos.z), color, DirectX::XMFLOAT2(u, v) };
                    vertices[vCount++] = { DirectX::XMFLOAT3(vpos.x + rightX + upX, vpos.y + rightY + upY, vpos.z), color, DirectX::XMFLOAT2(u + segSize, v) };
                    quadCount++;
                }
            }
            ctx->Unmap(ptclVB.Get(), 0);

            if (quadCount > 0) {
                UINT stride = sizeof(VertexPtcl); UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, ptclVB.GetAddressOf(), &stride, &offset);
                ctx->IASetIndexBuffer(ptclIB.Get(), DXGI_FORMAT_R16_UINT, 0);
                ctx->DrawIndexed(quadCount * 6, 0, 0);
            }

            ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
        }

        void DrawShapes(const Resource::C3Model& model, ShapeRenderState& state, float x, float y, int textureId, int frame, float angle, float pitch, float scale, int asb, int adb, const Resource::C3Model* parentModel, int linkBoneIndex, int parentFrame, int colorEnable, bool forceLocal) {
            if (model.shapes.empty()) return;
            const auto& shape = model.shapes[0];
            if (shape.lines.empty() || shape.lines[0].points.empty()) return;

            auto& d3d = D3DContext::GetInstance();
            auto ctx = d3d.context;

            if (state.segCount == 0) state.Initialize(shape.segmentCount);

            DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(0.0f, (float)d3d.screenWidth, (float)d3d.screenHeight, 0.0f, -1000.0f, 1000.0f);
            DirectX::XMMATRIX localPitch = DirectX::XMMatrixRotationX(pitch);
            DirectX::XMMATRIX rotZ = DirectX::XMMatrixRotationZ(angle);
            DirectX::XMMATRIX rotX = DirectX::XMMatrixRotationX(1.04719f);
            float s = 0.6f * scale;
            DirectX::XMMATRIX modelScale = DirectX::XMMatrixScaling(s, -s, s);

            DirectX::XMMATRIX attachmentMat = DirectX::XMMatrixIdentity();
            if (parentModel != nullptr && linkBoneIndex >= 0 && linkBoneIndex < (int)parentModel->motions.size()) {
                if (parentModel->motions[linkBoneIndex].boneCount > 0) {
                    attachmentMat = GetInterpolatedBone(parentModel->motions[linkBoneIndex], 0, parentFrame);
                }
            }

            DirectX::XMMATRIX world = localPitch * attachmentMat * rotZ * rotX * modelScale * DirectX::XMMatrixTranslation(x, y, 0.0f);
            DirectX::XMMATRIX mm = forceLocal ? DirectX::XMMatrixIdentity() : world;

            auto p0 = shape.lines[0].points[0];
            auto p1 = shape.lines[0].points.size() > 1 ? shape.lines[0].points[1] : p0;

            DirectX::XMVECTOR vecA = DirectX::XMVector3Transform(DirectX::XMVectorSet(p0.x, p0.y, p0.z, 1.0f), mm);
            DirectX::XMVECTOR vecB = DirectX::XMVector3Transform(DirectX::XMVectorSet(p1.x, p1.y, p1.z, 1.0f), mm);

            DirectX::XMFLOAT3 fVecA, fVecB;
            DirectX::XMStoreFloat3(&fVecA, vecA);
            DirectX::XMStoreFloat3(&fVecB, vecB);

            const int SMOOTH = 10;

            auto WriteSegment = [&](const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& prevA, const DirectX::XMFLOAT3& prevB) {
                int cur = state.segCur * 6;
                state.vb[cur + 0] = { a.x, a.y, a.z, 1, 1, 1, 1, 0, 0 };
                state.vb[cur + 1] = { b.x, b.y, b.z, 1, 1, 1, 1, 0, 1 };
                state.vb[cur + 2] = { prevB.x, prevB.y, prevB.z, 1, 1, 1, 1, 0, 1 };
                state.vb[cur + 3] = { prevA.x, prevA.y, prevA.z, 1, 1, 1, 1, 0, 0 };
                state.vb[cur + 4] = { prevB.x, prevB.y, prevB.z, 1, 1, 1, 1, 0, 1 };
                state.vb[cur + 5] = { a.x, a.y, a.z, 1, 1, 1, 1, 0, 0 };
                state.segCur = (state.segCur + 1) % state.segCount;
                };

            auto SetSegmentUV = [&](int seg, float u, float step) {
                int b = seg * 6;
                state.vb[b + 0].u = u; state.vb[b + 0].v = 0;
                state.vb[b + 1].u = u; state.vb[b + 1].v = 1;
                state.vb[b + 5].u = u; state.vb[b + 5].v = 0;
                u -= step;
                state.vb[b + 2].u = u; state.vb[b + 2].v = 1;
                state.vb[b + 3].u = u; state.vb[b + 3].v = 0;
                state.vb[b + 4].u = u; state.vb[b + 4].v = 1;
                };

            if (state.isFirst) {
                for (auto& v : state.vb) { v.px = v.py = v.pz = 0; v.a = 0; }
                state.isFirst = false;
            }
            else {
                DirectX::XMVECTOR prevAVec = DirectX::XMVectorSet(state.lastAx, state.lastAy, state.lastAz, 0.0f);
                DirectX::XMVECTOR prevBVec = DirectX::XMVectorSet(state.lastBx, state.lastBy, state.lastBz, 0.0f);

                DirectX::XMVECTOR distVec = DirectX::XMVector3Length(DirectX::XMVectorSubtract(vecA, vecB));
                float len = DirectX::XMVectorGetX(distVec);

                DirectX::XMFLOAT3 currentA = { state.lastAx, state.lastAy, state.lastAz };
                DirectX::XMFLOAT3 currentB = { state.lastBx, state.lastBy, state.lastBz };

                for (int nn = 0; nn < SMOOTH; nn++) {
                    float t = (nn + 1.0f) / (SMOOTH + 1.0f);
                    DirectX::XMVECTOR sAVec = DirectX::XMVectorLerp(prevAVec, vecA, t);
                    DirectX::XMVECTOR sBVec = DirectX::XMVectorLerp(prevBVec, vecB, t);

                    DirectX::XMVECTOR lnowVec = DirectX::XMVector3Length(DirectX::XMVectorSubtract(sAVec, sBVec));
                    float lnow = DirectX::XMVectorGetX(lnowVec);

                    if (lnow > 0.0001f) {
                        sAVec = DirectX::XMVectorLerp(sBVec, sAVec, len / lnow);
                    }

                    DirectX::XMFLOAT3 sA, sB;
                    DirectX::XMStoreFloat3(&sA, sAVec);
                    DirectX::XMStoreFloat3(&sB, sBVec);

                    WriteSegment(sA, sB, currentA, currentB);
                    currentA = sA; currentB = sB;
                }
                WriteSegment(fVecA, fVecB, currentA, currentB);

                float uvStep = 0.9f / state.segCount;
                float u = state.segCount * uvStep + 0.05f;

                for (int n = state.segCur - 1; n >= 0; n--) { SetSegmentUV(n, u, uvStep); u -= uvStep; }
                for (int n = state.segCount - 1; n > state.segCur; n--) { SetSegmentUV(n, u, uvStep); u -= uvStep; }
            }

            state.lastAx = fVecA.x; state.lastAy = fVecA.y; state.lastAz = fVecA.z;
            state.lastBx = fVecB.x; state.lastBy = fVecB.y; state.lastBz = fVecB.z;

            ctx->OMSetDepthStencilState(depthState2D.Get(), 0);

            int actualColorEnable = colorEnable % 100;
            ApplyBlendState(adb, actualColorEnable);

            ConstantBuffer cb;
            cb.WVP = DirectX::XMMatrixTranspose(forceLocal ? (world * ortho) : ortho);
            cb.UVRect = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);
            ctx->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

            ctx->IASetInputLayout(layoutPtcl.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(vsPtcl.Get(), nullptr, 0);
            ctx->VSSetConstantBuffers(0, 1, constantBuffer.GetAddressOf());
            ctx->PSSetShader(psPtcl.Get(), nullptr, 0);

            if (textures.count(textureId)) {
                ctx->PSSetShaderResources(0, 1, textures[textureId].GetAddressOf());
                ctx->PSSetSamplers(0, 1, samplerState.GetAddressOf());
            }

            D3D11_MAPPED_SUBRESOURCE mappedData;
            ctx->Map(ptclVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData);
            VertexPtcl* vertices = (VertexPtcl*)mappedData.pData;

            int drawCount = 0;
            for (int i = 0; i < state.segCount * 6; i++) {
                if (state.vb[i].a > 0) {
                    vertices[i] = {
                        DirectX::XMFLOAT3(state.vb[i].px, state.vb[i].py, state.vb[i].pz),
                        DirectX::XMFLOAT4(state.vb[i].r, state.vb[i].g, state.vb[i].b, state.vb[i].a),
                        DirectX::XMFLOAT2(state.vb[i].u, state.vb[i].v)
                    };
                    drawCount++;
                }
            }
            ctx->Unmap(ptclVB.Get(), 0);

            if (drawCount > 0) {
                UINT stride = sizeof(VertexPtcl); UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, ptclVB.GetAddressOf(), &stride, &offset);
                ctx->Draw(drawCount, 0);
            }

            ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
        }
    };

    struct SceneRenderer::Impl {
        MasterRenderer renderer;
        void Initialize(HWND hwnd, int w, int h) { D3DContext::GetInstance().Initialize(hwnd, w, h); renderer.Initialize(); }
        void Resize(int w, int h) { D3DContext::GetInstance().Resize(w, h); }
        void BeginFrame() { D3DContext::GetInstance().BeginFrame(0.05f, 0.05f, 0.05f); }
        void DrawSprite(int id, int x, int y, int w, int h) { renderer.DrawSprite(id, x, y, w, h); }

        void DrawMesh3D(const Resource::C3Model& model, float x, float y, int texId, int frame, float angle, float pitch, bool isPlayer, float scale, const Resource::C3Model* parentModel, int linkBoneIndex, int parentFrame, int asb, int adb, float alpha, bool disableZWrite, int colorEnable) {
            renderer.DrawMesh3D(model, x, y, texId, frame, angle, pitch, isPlayer, scale, parentModel, linkBoneIndex, parentFrame, asb, adb, alpha, disableZWrite, colorEnable);
        }

        void DrawParticles(const Resource::C3Model& model, float x, float y, int texId, int frame, float angle, float pitch, float scale, int asb, int adb, const Resource::C3Model* parentModel, int linkBoneIndex, int parentFrame, int colorEnable) {
            renderer.DrawParticles(model, x, y, texId, frame, angle, pitch, scale, asb, adb, parentModel, linkBoneIndex, parentFrame, colorEnable);
        }

        void DrawShapes(const Resource::C3Model& model, ShapeRenderState& state, float x, float y, int texId, int frame, float angle, float pitch, float scale, int asb, int adb, const Resource::C3Model* parentModel, int linkBoneIndex, int parentFrame, int colorEnable, bool forceLocal) {
            renderer.DrawShapes(model, state, x, y, texId, frame, angle, pitch, scale, asb, adb, parentModel, linkBoneIndex, parentFrame, colorEnable, forceLocal);
        }

        void EndFrame() { D3DContext::GetInstance().EndFrame(); }
        void LoadTexture(const wchar_t* filename) {}
        int LoadTextureFromMemory(const uint8_t* data, size_t size) { return renderer.LoadTextureFromMemory(data, size); }
        void DeleteTexture(int id) { renderer.DeleteTexture(id); }
    };

    SceneRenderer::SceneRenderer() : pImpl(new Impl()) {}
    SceneRenderer::~SceneRenderer() { delete pImpl; }
    void SceneRenderer::Initialize(HWND hwnd, int w, int h) { pImpl->Initialize(hwnd, w, h); }
    void SceneRenderer::Resize(int w, int h) { pImpl->Resize(w, h); }
    void SceneRenderer::BeginFrame() { pImpl->BeginFrame(); }
    void SceneRenderer::DrawSprite(int id, int x, int y, int w, int h) { pImpl->DrawSprite(id, x, y, w, h); }

    void SceneRenderer::DrawMesh3D(const Resource::C3Model& m, float x, float y, int texId, int f, float angle, float pitch, bool isPlayer, float scale, const Resource::C3Model* pModel, int boneIdx, int parentFrame, int asb, int adb, float alpha, bool disableZWrite, int colorEnable) {
        pImpl->DrawMesh3D(m, x, y, texId, f, angle, pitch, isPlayer, scale, pModel, boneIdx, parentFrame, asb, adb, alpha, disableZWrite, colorEnable);
    }

    void SceneRenderer::DrawParticles(const Resource::C3Model& m, float x, float y, int texId, int f, float angle, float pitch, float scale, int asb, int adb, const Resource::C3Model* pModel, int boneIdx, int parentFrame, int colorEnable) {
        pImpl->DrawParticles(m, x, y, texId, f, angle, pitch, scale, asb, adb, pModel, boneIdx, parentFrame, colorEnable);
    }

    void SceneRenderer::DrawShapes(const Resource::C3Model& m, ShapeRenderState& state, float x, float y, int texId, int f, float angle, float pitch, float scale, int asb, int adb, const Resource::C3Model* pModel, int boneIdx, int parentFrame, int colorEnable, bool forceLocal) {
        pImpl->DrawShapes(m, state, x, y, texId, f, angle, pitch, scale, asb, adb, pModel, boneIdx, parentFrame, colorEnable, forceLocal);
    }

    void SceneRenderer::EndFrame() { pImpl->EndFrame(); }
    void SceneRenderer::LoadTexture(const wchar_t* filename) { pImpl->LoadTexture(filename); }
    int SceneRenderer::LoadTextureFromMemory(const uint8_t* data, size_t size) { return pImpl->LoadTextureFromMemory(data, size); }
    void SceneRenderer::DeleteTexture(int id) { pImpl->DeleteTexture(id); }

    void* SceneRenderer::GetD3DDevice() { return D3DContext::GetInstance().device.Get(); }
    void* SceneRenderer::GetD3DContext() { return D3DContext::GetInstance().context.Get(); }
}