#include "pch.h"
#include "Graphics_Riot.h"
#include "RiotModel.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <unordered_map>
#include <memory>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace GraphicsRiot {

    using namespace DirectX;
    using Microsoft::WRL::ComPtr;

    namespace {
        ID3D11Device* g_device = nullptr;
        ID3D11DeviceContext* g_context = nullptr;

        int g_screenWidth = 0;
        int g_screenHeight = 0;

        std::unordered_map<RiotModelHandle, std::unique_ptr<Riot::RiotModel>> g_models;
        RiotModelHandle g_nextHandle = 1;

        ComPtr<ID3D11VertexShader> g_vs;
        ComPtr<ID3D11PixelShader> g_ps;
        ComPtr<ID3D11InputLayout> g_layout;
        ComPtr<ID3D11Buffer> g_constantBuffer;
        ComPtr<ID3D11SamplerState> g_sampler;
        ComPtr<ID3D11RasterizerState> g_rasterState;
        ComPtr<ID3D11DepthStencilState> g_depthState;
        ComPtr<ID3D11BlendState> g_blendState;

        bool g_pipelineReady = false;

        struct RiotConstantBuffer {
            XMMATRIX WVP;
            float Alpha;
            float pad[3];
        };

        const char* g_vsCode = R"(
            cbuffer ConstantBuffer : register(b0) { matrix WVP; float Alpha; float3 pad; }
            struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
            VOut main(float3 pos : POSITION, float3 normal : NORMAL, float2 tex : TEXCOORD) {
                VOut output;
                output.position = mul(float4(pos, 1.0f), WVP);
                output.tex = tex;
                return output;
            }
        )";

        const char* g_psCode = R"(
            cbuffer ConstantBuffer : register(b0) { matrix WVP; float Alpha; float3 pad; }
            Texture2D shaderTexture : register(t0);
            SamplerState sampleType : register(s0);
            struct VOut { float4 position : SV_POSITION; float2 tex : TEXCOORD; };
            float4 main(VOut input) : SV_TARGET {
                float4 color = shaderTexture.Sample(sampleType, input.tex);
                clip(color.a * Alpha - 0.05f);
                return float4(color.rgb, color.a * Alpha);
            }
        )";

        bool EnsurePipeline() {
            if (g_pipelineReady) return true;
            if (!g_device) return false;

            ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
            HRESULT hr = D3DCompile(g_vsCode, strlen(g_vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
            if (FAILED(hr)) return false;
            hr = D3DCompile(g_psCode, strlen(g_psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errBlob);
            if (FAILED(hr)) return false;

            g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
            g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);

            D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            g_device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_layout);

            D3D11_BUFFER_DESC cbDesc = {};
            cbDesc.Usage = D3D11_USAGE_DEFAULT;
            cbDesc.ByteWidth = sizeof(RiotConstantBuffer);
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            g_device->CreateBuffer(&cbDesc, nullptr, &g_constantBuffer);

            D3D11_SAMPLER_DESC sampDesc = {};
            sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            g_device->CreateSamplerState(&sampDesc, &g_sampler);

            D3D11_RASTERIZER_DESC rastDesc = {};
            rastDesc.FillMode = D3D11_FILL_SOLID;
            rastDesc.CullMode = D3D11_CULL_NONE;
            g_device->CreateRasterizerState(&rastDesc, &g_rasterState);

            D3D11_DEPTH_STENCIL_DESC dsDesc = {};
            dsDesc.DepthEnable = TRUE;
            dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
            g_device->CreateDepthStencilState(&dsDesc, &g_depthState);

            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            g_device->CreateBlendState(&blendDesc, &g_blendState);

            g_pipelineReady = true;
            return true;
        }
    }

    void Initialize(void* d3dDevice, void* d3dContext, int screenWidth, int screenHeight) {
        g_device = reinterpret_cast<ID3D11Device*>(d3dDevice);
        g_context = reinterpret_cast<ID3D11DeviceContext*>(d3dContext);
        g_screenWidth = screenWidth;
        g_screenHeight = screenHeight;
        EnsurePipeline();
    }

    void Resize(int screenWidth, int screenHeight) {
        g_screenWidth = screenWidth;
        g_screenHeight = screenHeight;
    }

    RiotModelHandle LoadRiotChampion(const char* sknPath, const char* sklPath,
        const char* anmPath, const wchar_t* texturePath) {

        if (!g_device) return InvalidRiotModelHandle;

        auto model = std::make_unique<Riot::RiotModel>();

        std::string anm = anmPath ? anmPath : "";
        std::wstring tex = texturePath ? texturePath : L"";

        bool ok = model->LoadFromFiles(g_device, sknPath ? sknPath : "", sklPath ? sklPath : "", anm, tex);
        if (!ok) return InvalidRiotModelHandle;

        RiotModelHandle handle = g_nextHandle++;
        g_models[handle] = std::move(model);
        return handle;
    }

    void UnloadRiotChampion(RiotModelHandle handle) {
        g_models.erase(handle);
    }

    int GetRiotSubMeshCount(RiotModelHandle handle) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return 0;
        return it->second->GetSubMeshCount();
    }

    const char* GetRiotSubMeshName(RiotModelHandle handle, int subMeshIndex) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return "";
        return it->second->GetSubMeshName(subMeshIndex);
    }

    void SetRiotSubMeshVisible(RiotModelHandle handle, int subMeshIndex, bool visible) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return;
        it->second->SetSubMeshVisible(subMeshIndex, visible);
    }

    bool IsRiotSubMeshVisible(RiotModelHandle handle, int subMeshIndex) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return false;
        return it->second->IsSubMeshVisible(subMeshIndex);
    }

    bool SetRiotSubMeshTexture(RiotModelHandle handle, int subMeshIndex, const wchar_t* texturePath) {
        auto it = g_models.find(handle);
        if (it == g_models.end() || !g_device) return false;
        std::wstring tex = texturePath ? texturePath : L"";
        return it->second->SetSubMeshTexture(g_device, subMeshIndex, tex);
    }

    void UpdateRiotAnimation(RiotModelHandle handle, float deltaTime) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return;
        it->second->UpdateAnimation(g_context, deltaTime);
    }

    void DrawRiotModel(RiotModelHandle handle, float x, float y, float angle, float scale, float alpha) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return;
        if (!EnsurePipeline()) return;

        // Riot/LoL models are authored Y-up with the character facing -Z, while the rest of the
        // engine (C3 models) uses Z-up. Rotate -90 degrees around X to convert Y-up into Z-up
        // (old Y becomes new Z, old Z becomes new -Y), then flip 180 degrees around Z so the
        // model faces the same "south" direction as C3 characters instead of standing backwards.
        XMMATRIX axisFix = XMMatrixRotationX(-XM_PIDIV2) * XMMatrixRotationZ(XM_PI);

        // Riot models are authored in centimeters (a champion is roughly 180-200 units tall),
        // while C3 models are authored in a much smaller unit scale. kRiotModelScale brings a
        // champion down to roughly the same on-screen height as the player model.
        constexpr float kRiotModelScale = 1.60f;

        // Matches Graphics::SceneRenderer::DrawMesh3D screen-space convention: orthographic
        // projection over the back buffer, Z rotation for facing, X rotation to tilt into "3D",
        // uniform scale (with Y flip) and translation to the screen position.
        XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(0.0f, (float)g_screenWidth, (float)g_screenHeight, 0.0f, -1000.0f, 1000.0f);
        XMMATRIX rotZ = XMMatrixRotationZ(angle);
        XMMATRIX rotX = XMMatrixRotationX(1.04719f);
        float s = 0.6f * kRiotModelScale * scale;
        XMMATRIX modelScale = XMMatrixScaling(s, -s, s);
        XMMATRIX world = axisFix * rotZ * rotX * modelScale * XMMatrixTranslation(x, y, 0.0f);

        RiotConstantBuffer cb;
        cb.WVP = XMMatrixTranspose(world * ortho);
        cb.Alpha = alpha;
        g_context->UpdateSubresource(g_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

        g_context->OMSetDepthStencilState(g_depthState.Get(), 0);
        g_context->OMSetBlendState(g_blendState.Get(), nullptr, 0xFFFFFFFF);
        g_context->RSSetState(g_rasterState.Get());

        g_context->IASetInputLayout(g_layout.Get());
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_vs.Get(), nullptr, 0);
        g_context->PSSetShader(g_ps.Get(), nullptr, 0);
        g_context->VSSetConstantBuffers(0, 1, g_constantBuffer.GetAddressOf());
        g_context->PSSetConstantBuffers(0, 1, g_constantBuffer.GetAddressOf());
        g_context->PSSetSamplers(0, 1, g_sampler.GetAddressOf());

        it->second->Draw(g_context);
    }

    bool IsRiotModelValid(RiotModelHandle handle) {
        auto it = g_models.find(handle);
        if (it == g_models.end()) return false;
        return it->second->IsValid();
    }
}
