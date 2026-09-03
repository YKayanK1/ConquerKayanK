#include "pch.h"
#include "RiotModel.h"
#include "SknLoader.h"
#include "SklLoader.h"
#include "AnmLoader.h"

namespace Riot {

    namespace {
        XMFLOAT4X4 IdentityMatrix() {
            XMFLOAT4X4 m;
            XMStoreFloat4x4(&m, XMMatrixIdentity());
            return m;
        }

        // Linear search + lerp helpers for evaluating animation curves at an arbitrary time.
        XMFLOAT3 SampleTranslation(const AnmBoneTrack& track, float time, const XMFLOAT3& fallback) {
            if (track.translationFrames.empty()) return fallback;
            if (track.translationFrames.size() == 1) return track.translationFrames[0].value;

            for (size_t i = 0; i + 1 < track.translationFrames.size(); ++i) {
                const auto& a = track.translationFrames[i];
                const auto& b = track.translationFrames[i + 1];
                if (time >= a.time && time <= b.time) {
                    float span = (b.time - a.time);
                    float t = span > 0.0f ? (time - a.time) / span : 0.0f;
                    XMVECTOR va = XMLoadFloat3(&a.value);
                    XMVECTOR vb = XMLoadFloat3(&b.value);
                    XMVECTOR result = XMVectorLerp(va, vb, t);
                    XMFLOAT3 out;
                    XMStoreFloat3(&out, result);
                    return out;
                }
            }
            return track.translationFrames.back().value;
        }

        XMFLOAT4 SampleRotation(const AnmBoneTrack& track, float time, const XMFLOAT4& fallback) {
            if (track.rotationFrames.empty()) return fallback;
            if (track.rotationFrames.size() == 1) return track.rotationFrames[0].value;

            for (size_t i = 0; i + 1 < track.rotationFrames.size(); ++i) {
                const auto& a = track.rotationFrames[i];
                const auto& b = track.rotationFrames[i + 1];
                if (time >= a.time && time <= b.time) {
                    float span = (b.time - a.time);
                    float t = span > 0.0f ? (time - a.time) / span : 0.0f;
                    XMVECTOR qa = XMLoadFloat4(&a.value);
                    XMVECTOR qb = XMLoadFloat4(&b.value);

                    // Compressed quaternions can decode with either sign for the same rotation
                    // (double-cover). Slerp/lerp must take the shortest path, so flip qb when the
                    // dot product is negative, otherwise the interpolation twists through the "long way".
                    float dot = XMVectorGetX(XMQuaternionDot(qa, qb));
                    if (dot < 0.0f) qb = XMVectorNegate(qb);

                    XMVECTOR result = XMQuaternionSlerp(qa, qb, t);
                    XMFLOAT4 out;
                    XMStoreFloat4(&out, result);
                    return out;
                }
            }
            return track.rotationFrames.back().value;
        }

        XMFLOAT3 SampleScale(const AnmBoneTrack& track, float time, const XMFLOAT3& fallback) {
            if (track.scaleFrames.empty()) return fallback;
            for (size_t i = 0; i + 1 < track.scaleFrames.size(); ++i) {
                const auto& a = track.scaleFrames[i];
                const auto& b = track.scaleFrames[i + 1];
                if (time >= a.time && time <= b.time) {
                    float span = (b.time - a.time);
                    float t = span > 0.0f ? (time - a.time) / span : 0.0f;
                    XMVECTOR va = XMLoadFloat3(&a.value);
                    XMVECTOR vb = XMLoadFloat3(&b.value);
                    XMVECTOR result = XMVectorLerp(va, vb, t);
                    XMFLOAT3 out;
                    XMStoreFloat3(&out, result);
                    return out;
                }
            }
            return track.scaleFrames.back().value;
        }
    }

    bool RiotModel::LoadFromFiles(ID3D11Device* device, const std::string& sknPath, const std::string& sklPath,
        const std::string& anmPath, const std::wstring& texturePath) {

        skn = SknLoader::Load(sknPath);
        if (!skn.valid) {
            OutputDebugStringA(("[Riot] Falha ao carregar .skn: " + sknPath + "\n").c_str());
            return false;
        }

        skl = SklLoader::Load(sklPath);
        if (!skl.valid) {
            OutputDebugStringA(("[Riot] Falha ao carregar .skl: " + sklPath + "\n").c_str());
            return false;
        }

        if (!anmPath.empty()) {
            anm = AnmLoader::Load(anmPath);
            if (!anm.valid) {
                OutputDebugStringA(("[Riot] Aviso: falha ao carregar .anm (seguindo sem animacao): " + anmPath + "\n").c_str());
            }
        }

        if (!texturePath.empty()) {
            if (!RiotTexture::Load(device, texturePath, textureSRV)) {
                OutputDebugStringA("[Riot] Aviso: falha ao carregar textura (seguindo sem textura).\n");
            }
        }

        cachedDevice = device;

        subMeshRuntime.clear();
        subMeshRuntime.reserve(skn.subMeshes.size());
        for (const auto& sm : skn.subMeshes) {
            RiotSubMeshRuntime rt;
            rt.name = sm.name;
            rt.visible = true;
            subMeshRuntime.push_back(rt);
        }

        std::vector<XMFLOAT4X4> bindMatrices(skl.bones.size());
        for (size_t i = 0; i < skl.bones.size(); ++i) bindMatrices[i] = IdentityMatrix();
        RebuildSkinnedVertices(bindMatrices);

        valid = CreateGpuResources(device);
        if (!valid) {
            OutputDebugStringA("[Riot] Falha ao criar recursos de GPU (CreateGpuResources).\n");
        }
        return valid;
    }

    void RiotModel::ComputeBoneMatricesAtTime(float time, std::vector<XMFLOAT4X4>& outMatrices) const {
        outMatrices.resize(skl.bones.size());

        // Compute local->global transforms respecting parent hierarchy, then combine with inverse bind pose.
        std::vector<XMFLOAT4X4> globals(skl.bones.size());

#ifdef _DEBUG
        static bool logged = false;
        if (!logged && anm.valid) {
            logged = true;
            int matched = 0;
            for (const auto& bone : skl.bones) {
                if (anm.FindTrack(bone.nameHash)) ++matched;
            }
            char dbg[256];
            sprintf_s(dbg, "[Riot][Model] bones=%zu anmTracks=%zu matched=%d\n",
                skl.bones.size(), anm.tracks.size(), matched);
            OutputDebugStringA(dbg);
        }
#endif

        for (size_t i = 0; i < skl.bones.size(); ++i) {
            const SklBone& bone = skl.bones[i];

            XMFLOAT3 translation = bone.localPosition;
            XMFLOAT4 rotation = bone.localRotation;
            XMFLOAT3 scale = bone.localScale;

            if (anm.valid) {
                const AnmBoneTrack* track = anm.FindTrack(bone.nameHash);
                if (track) {
                    translation = SampleTranslation(*track, time, bone.localPosition);
                    rotation = SampleRotation(*track, time, bone.localRotation);
                    scale = SampleScale(*track, time, bone.localScale);
                }
            }

            XMMATRIX localMat = XMMatrixScaling(scale.x, scale.y, scale.z) *
                XMMatrixRotationQuaternion(XMLoadFloat4(&rotation)) *
                XMMatrixTranslation(translation.x, translation.y, translation.z);

            if (bone.parentId < 0) {
                XMStoreFloat4x4(&globals[i], localMat);
            } else {
                // parentId is a direct index into the bones array (matching file order), not an
                // arbitrary bone.id to search for; the reference parser indexes joints[parent_id] directly.
                int parentIndex = (bone.parentId < (int)skl.bones.size()) ? (int)bone.parentId : -1;
                if (parentIndex >= 0 && parentIndex < (int)i) {
                    XMMATRIX parentMat = XMLoadFloat4x4(&globals[parentIndex]);
                    XMStoreFloat4x4(&globals[i], localMat * parentMat);
                } else {
                    XMStoreFloat4x4(&globals[i], localMat);
                }
            }
        }

        for (size_t i = 0; i < skl.bones.size(); ++i) {
            XMMATRIX inverseBind = XMLoadFloat4x4(&skl.bones[i].inverseGlobalMatrix);
            XMMATRIX global = XMLoadFloat4x4(&globals[i]);
            XMStoreFloat4x4(&outMatrices[i], inverseBind * global);
        }
    }

    void RiotModel::RebuildSkinnedVertices(const std::vector<XMFLOAT4X4>& boneMatrices) {
        skinnedVertices.resize(skn.vertices.size());

        for (size_t i = 0; i < skn.vertices.size(); ++i) {
            const SknVertex& v = skn.vertices[i];

            XMVECTOR pos = XMVectorZero();
            XMVECTOR nrm = XMVectorZero();
            XMVECTOR srcPos = XMVectorSet(v.position.x, v.position.y, v.position.z, 1.0f);
            XMVECTOR srcNrm = XMVectorSet(v.normal.x, v.normal.y, v.normal.z, 0.0f);

            float totalWeight = 0.0f;
            for (int b = 0; b < 4; ++b) {
                float w = v.weights[b];
                if (w <= 0.0f) continue;
                uint8_t rawIdx = v.boneIndices[b];
                // .skn vertex bone indices are not direct bone-array indices; they must be
                // remapped through the .skl influences table (see SklLoader).
                uint32_t boneIdx = rawIdx;
                if (rawIdx < skl.influences.size()) boneIdx = skl.influences[rawIdx];
                if (boneIdx >= boneMatrices.size()) continue;

                XMMATRIX m = XMLoadFloat4x4(&boneMatrices[boneIdx]);
                pos += XMVector3Transform(srcPos, m) * w;
                nrm += XMVector3TransformNormal(srcNrm, m) * w;
                totalWeight += w;
            }

            if (totalWeight <= 0.0001f) {
                pos = srcPos;
                nrm = srcNrm;
            }

            RiotGpuVertex out;
            XMStoreFloat3(&out.position, pos);
            XMStoreFloat3(&out.normal, XMVector3Normalize(nrm));
            out.uv = v.uv;
            skinnedVertices[i] = out;
        }
    }

    bool RiotModel::CreateGpuResources(ID3D11Device* device) {
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.ByteWidth = (UINT)(sizeof(RiotGpuVertex) * skinnedVertices.size());
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = skinnedVertices.data();

        HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, vertexBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.ByteWidth = (UINT)(sizeof(uint16_t) * skn.indices.size());
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = skn.indices.data();

        hr = device->CreateBuffer(&ibDesc, &ibData, indexBuffer.GetAddressOf());
        return SUCCEEDED(hr);
    }

    void RiotModel::UpdateAnimation(ID3D11DeviceContext* context, float deltaTime) {
        if (!valid) return;

        if (anm.valid && anm.duration > 0.0f) {
            animTime += deltaTime;
            if (animTime > anm.duration) animTime = fmodf(animTime, anm.duration);
        }

        std::vector<XMFLOAT4X4> boneMatrices;
        ComputeBoneMatricesAtTime(animTime, boneMatrices);

#ifdef _DEBUG
        {
            static float lastLogTime = -1.0f;
            static int frameLogCount = 0;
            if (frameLogCount < 6) {
                ++frameLogCount;
                size_t totalRot = 0, totalTrans = 0, totalScale = 0;
                for (const auto& t : anm.tracks) {
                    totalRot += t.rotationFrames.size();
                    totalTrans += t.translationFrames.size();
                    totalScale += t.scaleFrames.size();
                }
                size_t midIdx = skl.bones.size() / 2;
                char dbg[512];
                sprintf_s(dbg, "[Riot][Anim] animTime=%.3f duration=%.3f totalRot=%zu totalTrans=%zu totalScale=%zu midBone(%zu name=%s)._41=%.3f _42=%.3f _43=%.3f\n",
                    animTime, anm.duration, totalRot, totalTrans, totalScale,
                    midIdx, skl.bones.empty() ? "?" : skl.bones[midIdx].name.c_str(),
                    boneMatrices.empty() ? 0.0f : boneMatrices[midIdx]._41,
                    boneMatrices.empty() ? 0.0f : boneMatrices[midIdx]._42,
                    boneMatrices.empty() ? 0.0f : boneMatrices[midIdx]._43);
                OutputDebugStringA(dbg);
            }
        }
#endif

        RebuildSkinnedVertices(boneMatrices);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = context->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, skinnedVertices.data(), sizeof(RiotGpuVertex) * skinnedVertices.size());
            context->Unmap(vertexBuffer.Get(), 0);
        }
    }

    void RiotModel::Draw(ID3D11DeviceContext* context) {
        if (!valid) return;

        UINT stride = sizeof(RiotGpuVertex);
        UINT offset = 0;
        context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Sem tabela de submeshes (versao antiga do .skn): desenha tudo de uma vez com a
        // textura padrao, como antes.
        if (skn.subMeshes.empty() || subMeshRuntime.empty()) {
            if (textureSRV) context->PSSetShaderResources(0, 1, textureSRV.GetAddressOf());
            context->DrawIndexed((UINT)skn.indices.size(), 0, 0);
            return;
        }

        for (size_t i = 0; i < skn.subMeshes.size(); ++i) {
            if (i >= subMeshRuntime.size() || !subMeshRuntime[i].visible) continue;
            const SknSubMesh& sm = skn.subMeshes[i];
            if (sm.indexCount == 0) continue;

            ID3D11ShaderResourceView* srv = subMeshRuntime[i].overrideTextureSRV
                ? subMeshRuntime[i].overrideTextureSRV.Get()
                : textureSRV.Get();
            if (srv) context->PSSetShaderResources(0, 1, &srv);

            context->DrawIndexed(sm.indexCount, sm.indexOffset, 0);
        }
    }
}
