// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#include "pch.h"
#include "Graphics.h"
#include "Graphics_D3D.h"
#include "Graphics_Shaders.h"

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

namespace Graphics {

    struct Vertex2D {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT2 Tex;
    };

    struct ConstantBuffer {
        DirectX::XMMATRIX WVP;
        DirectX::XMMATRIX Bones[128];
        int HasAnimation;
        float Alpha;
        DirectX::XMFLOAT2 padding;
    };

    struct VertexPtcl {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT4 Color;
        DirectX::XMFLOAT2 Tex;
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
        std::unordered_map<std::string, MeshCache> m_meshCache;

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
            dsDescNW.DepthEnable = TRUE;
            dsDescNW.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDescNW.DepthFunc = D3D11_COMPARISON_LESS;
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
            addDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            addDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
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

        void DrawSprite(int textureId, int x, int y, int width, int height) {
            auto& d3d = D3DContext::GetInstance(); auto ctx = d3d.context;

            ctx->OMSetDepthStencilState(depthState2D.Get(), 0);

            DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(0.0f, (float)d3d.screenWidth, (float)d3d.screenHeight, 0.0f, 0.0f, 1.0f);
            DirectX::XMMATRIX world = DirectX::XMMatrixScaling((float)width, (float)height, 1.0f) * DirectX::XMMatrixTranslation((float)x, (float)y, 0.0f);

            ConstantBuffer cb; cb.WVP = DirectX::XMMatrixTranspose(world * ortho);
            cb.HasAnimation = 0;
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

        void DrawMesh3D(const Resource::C3Model& model, float x, float y, int textureId, int frame, float angle, float pitch, bool isPlayer, float scale, const Resource::C3Model* parentModel, int linkBoneIndex, const std::string& effectName, int asb, int adb, float alpha, bool disableZWrite) {
            if (model.phys.empty()) return;

            auto& d3d = D3DContext::GetInstance();
            auto ctx = d3d.context;

            if (disableZWrite) {
                ctx->OMSetDepthStencilState(depthStateNoWrite.Get(), 0);
            }
            else {
                ctx->OMSetDepthStencilState(depthState3D.Get(), 0);
            }

            if (adb == 2) {
                ctx->OMSetBlendState(blendStateAdditive.Get(), nullptr, 0xFFFFFFFF);
            }
            else {
                ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
            }

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
                    attachmentMat = GetInterpolatedBone(parentModel->motions[linkBoneIndex], 0, frame);
                }
            }

            DirectX::XMMATRIX localPitch = DirectX::XMMatrixRotationX(pitch);
            DirectX::XMMATRIX world = localPitch * attachmentMat * rotZ * rotX * modelScale * DirectX::XMMatrixTranslation(x, y, 0.0f);

            ConstantBuffer cb;
            cb.HasAnimation = 1;
            cb.Alpha = alpha;
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

                std::string cacheKey = phy.name + "_" + std::to_string(phy.vertices.size()) + "_" + std::to_string(phy.indices.size());

                if (m_meshCache.find(cacheKey) == m_meshCache.end()) {
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

                    m_meshCache[cacheKey] = cache;
                }

                auto& cache = m_meshCache[cacheKey];
                UINT stride = sizeof(Resource::PhyVertex);
                UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, cache.vb.GetAddressOf(), &stride, &offset);
                ctx->IASetIndexBuffer(cache.ib.Get(), DXGI_FORMAT_R16_UINT, 0);

                ctx->DrawIndexed(cache.indexCount, 0, 0);
            }
            ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
        }

        void DrawParticles(const Resource::C3Model& model, float x, float y, int textureId, int frame, float angle, float pitch, float scale, int asb, int adb) {
            if (model.ptcls.empty()) return;

            auto& d3d = D3DContext::GetInstance();
            auto ctx = d3d.context;

            ctx->OMSetDepthStencilState(depthState2D.Get(), 0);

            if (adb == 2) {
                ctx->OMSetBlendState(blendStateAdditive.Get(), nullptr, 0xFFFFFFFF);
            }
            else {
                ctx->OMSetBlendState(blendStateAlpha.Get(), nullptr, 0xFFFFFFFF);
            }

            DirectX::XMMATRIX ortho = DirectX::XMMatrixOrthographicOffCenterLH(
                0.0f, (float)d3d.screenWidth, (float)d3d.screenHeight, 0.0f, -1000.0f, 1000.0f
            );

            DirectX::XMMATRIX localPitch = DirectX::XMMatrixRotationX(pitch);
            DirectX::XMMATRIX rotZ = DirectX::XMMatrixRotationZ(angle);
            DirectX::XMMATRIX rotX = DirectX::XMMatrixRotationX(1.04719f);

            float s = 0.6f * scale;
            DirectX::XMMATRIX modelScale = DirectX::XMMatrixScaling(s, -s, s);
            DirectX::XMMATRIX world = localPitch * rotZ * rotX * modelScale * DirectX::XMMatrixTranslation(x, y, 0.0f);

            ConstantBuffer cb;
            cb.WVP = DirectX::XMMatrixTranspose(ortho);
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
                    DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, alpha };

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
    };

    struct SceneRenderer::Impl {
        MasterRenderer renderer;
        void Initialize(HWND hwnd, int w, int h) { D3DContext::GetInstance().Initialize(hwnd, w, h); renderer.Initialize(); }
        void Resize(int w, int h) { D3DContext::GetInstance().Resize(w, h); }
        void BeginFrame() { D3DContext::GetInstance().BeginFrame(0.05f, 0.05f, 0.05f); }
        void DrawSprite(int id, int x, int y, int w, int h) { renderer.DrawSprite(id, x, y, w, h); }
        void DrawMesh3D(const Resource::C3Model& model, float x, float y, int texId, int frame, float angle, float pitch, bool isPlayer, float scale, const Resource::C3Model* parentModel, int linkBoneIndex, const std::string& effectName, int asb, int adb, float alpha, bool disableZWrite) {
            renderer.DrawMesh3D(model, x, y, texId, frame, angle, pitch, isPlayer, scale, parentModel, linkBoneIndex, effectName, asb, adb, alpha, disableZWrite);
        }
        void DrawParticles(const Resource::C3Model& model, float x, float y, int texId, int frame, float angle, float pitch, float scale, int asb, int adb) {
            renderer.DrawParticles(model, x, y, texId, frame, angle, pitch, scale, asb, adb);
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
    void SceneRenderer::DrawMesh3D(const Resource::C3Model& m, float x, float y, int texId, int f, float angle, float pitch, bool isPlayer, float scale, const Resource::C3Model* pModel, int boneIdx, const std::string& effectName, int asb, int adb, float alpha, bool disableZWrite) {
        pImpl->DrawMesh3D(m, x, y, texId, f, angle, pitch, isPlayer, scale, pModel, boneIdx, effectName, asb, adb, alpha, disableZWrite);
    }
    void SceneRenderer::DrawParticles(const Resource::C3Model& m, float x, float y, int texId, int f, float angle, float pitch, float scale, int asb, int adb) {
        pImpl->DrawParticles(m, x, y, texId, f, angle, pitch, scale, asb, adb);
    }
    void SceneRenderer::EndFrame() { pImpl->EndFrame(); }
    void SceneRenderer::LoadTexture(const wchar_t* filename) { pImpl->LoadTexture(filename); }
    int SceneRenderer::LoadTextureFromMemory(const uint8_t* data, size_t size) { return pImpl->LoadTextureFromMemory(data, size); }
    void SceneRenderer::DeleteTexture(int id) { pImpl->DeleteTexture(id); }
}