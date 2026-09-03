#include "pch.h"
#include "SklLoader.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace Riot {

    namespace {
        constexpr uint32_t kClassicSignature = 0x746C6B73u; // 'stkl' or similar 4cc used as type marker
        constexpr uint32_t kVersion2Signature = 0x22FD4FC3u;

        template<typename T>
        bool ReadRaw(std::ifstream& f, T& out) {
            f.read(reinterpret_cast<char*>(&out), sizeof(T));
            return (bool)f;
        }

        XMFLOAT4X4 MakeTRS(const XMFLOAT3& pos, const XMFLOAT4& rotQuat, const XMFLOAT3& scale) {
            XMVECTOR q = XMLoadFloat4(&rotQuat);
            XMMATRIX rot = XMMatrixRotationQuaternion(q);
            XMMATRIX scl = XMMatrixScaling(scale.x, scale.y, scale.z);
            XMMATRIX trans = XMMatrixTranslation(pos.x, pos.y, pos.z);
            XMMATRIX result = scl * rot * trans;
            XMFLOAT4X4 out;
            XMStoreFloat4x4(&out, result);
            return out;
        }

        XMFLOAT4X4 InvertMatrix(const XMFLOAT4X4& m) {
            XMMATRIX mat = XMLoadFloat4x4(&m);
            XMVECTOR det;
            XMMATRIX inv = XMMatrixInverse(&det, mat);
            XMFLOAT4X4 out;
            XMStoreFloat4x4(&out, inv);
            return out;
        }

        void ReadClassic(std::ifstream& f, SklModel& model) {
            // Classic format: after signature (already consumed) - version(u32), designerId(u32)? followed by bone count.
            uint32_t version = 0;
            ReadRaw(f, version);

            uint32_t boneCount = 0;
            ReadRaw(f, boneCount);

            model.bones.resize(boneCount);
            for (uint32_t i = 0; i < boneCount; ++i) {
                char name[32] = {};
                f.read(name, sizeof(name));

                int32_t parentId = -1;
                ReadRaw(f, parentId);

                float scale = 1.0f;
                ReadRaw(f, scale);

                float matrix[3][4] = {};
                f.read(reinterpret_cast<char*>(matrix), sizeof(matrix));

                SklBone bone;
                bone.name = std::string(name);
                bone.nameHash = ElfHash(bone.name);
                bone.id = (int16_t)i;
                bone.parentId = (int16_t)parentId;
                bone.localScale = { scale, scale, scale };

                XMFLOAT4X4 global{};
                global._11 = matrix[0][0]; global._21 = matrix[0][1]; global._31 = matrix[0][2]; global._41 = matrix[0][3];
                global._12 = matrix[1][0]; global._22 = matrix[1][1]; global._32 = matrix[1][2]; global._42 = matrix[1][3];
                global._13 = matrix[2][0]; global._23 = matrix[2][1]; global._33 = matrix[2][2]; global._43 = matrix[2][3];
                global._14 = 0; global._24 = 0; global._34 = 0; global._44 = 1;

                bone.globalMatrix = global;
                bone.inverseGlobalMatrix = InvertMatrix(global);
                bone.localPosition = { global._41, global._42, global._43 };

                model.bones[i] = bone;
            }

            // Classic .skl always uses an identity bone-index mapping (no separate influences table).
            model.influences.resize(boneCount);
            for (uint32_t i = 0; i < boneCount; ++i) model.influences[i] = (uint16_t)i;
        }

        void ReadVersion2(std::ifstream& f, SklModel& model) {
            // Signature(u32) + type(u32) were already consumed by the caller; the reference parser
            // seeks to absolute position 8 here (right after signature+type), then reads version.
            f.seekg(8, std::ios::beg);

            uint32_t version = 0;
            ReadRaw(f, version);

            f.seekg(2, std::ios::cur); // unused 2 bytes

            uint16_t boneCount = 0;
            ReadRaw(f, boneCount);

            uint32_t boneIndexCount = 0;
            ReadRaw(f, boneIndexCount);

            uint32_t boneOffset = 0;
            ReadRaw(f, boneOffset);

            f.seekg(4, std::ios::cur); // unused 4 bytes

            uint32_t boneIndicesOffset = 0;
            ReadRaw(f, boneIndicesOffset);

            f.seekg((std::streamoff)boneOffset, std::ios::beg);

            model.bones.resize(boneCount);
            for (uint16_t i = 0; i < boneCount; ++i) {
                SklBone bone;

                f.seekg(2, std::ios::cur); // unused 2 bytes

                int16_t id = -1;
                ReadRaw(f, id);
                bone.id = id;

                int16_t parentId = -1;
                ReadRaw(f, parentId);
                bone.parentId = parentId;

                f.seekg(2, std::ios::cur); // unused 2 bytes

                uint32_t hash = 0;
                ReadRaw(f, hash);
                bone.nameHash = hash;

                f.seekg(4, std::ios::cur); // unused 4 bytes

                XMFLOAT3 position{}, scale{};
                XMFLOAT4 rotation{};
                ReadRaw(f, position.x); ReadRaw(f, position.y); ReadRaw(f, position.z);
                ReadRaw(f, scale.x); ReadRaw(f, scale.y); ReadRaw(f, scale.z);
                ReadRaw(f, rotation.x); ReadRaw(f, rotation.y); ReadRaw(f, rotation.z); ReadRaw(f, rotation.w);

                bone.localPosition = position;
                bone.localScale = scale;
                bone.localRotation = rotation;
                bone.globalMatrix = MakeTRS(position, rotation, scale);

                // Inverse bind pose (position/scale/rotation), stored directly by the file; using
                // it as-is (instead of recomputing) matches the exact pose the mesh was skinned against.
                XMFLOAT3 invPosition{}, invScale{};
                XMFLOAT4 invRotation{};
                ReadRaw(f, invPosition.x); ReadRaw(f, invPosition.y); ReadRaw(f, invPosition.z);
                ReadRaw(f, invScale.x); ReadRaw(f, invScale.y); ReadRaw(f, invScale.z);
                ReadRaw(f, invRotation.x); ReadRaw(f, invRotation.y); ReadRaw(f, invRotation.z); ReadRaw(f, invRotation.w);

                bone.inverseGlobalMatrix = MakeTRS(invPosition, invRotation, invScale);

                // Name offset is relative to the position right after this 4-byte field itself
                // (i.e. "current position after reading the offset" + name_offset), matching the
                // reference parser's return_offset - 4 + name_offset convention.
                int32_t nameOffset = 0;
                ReadRaw(f, nameOffset);
                std::streamoff returnOffset = f.tellg();

                f.seekg(returnOffset - 4 + nameOffset, std::ios::beg);
                std::string name;
                char c = 0;
                while (f.get(c) && c != '\0') name.push_back(c);
                bone.name = name;

                f.seekg(returnOffset, std::ios::beg);

                model.bones[i] = bone;
            }

            // The .skn vertex bone indices (0-3) are NOT direct indices into this bone array;
            // they index into a separate remapping table (bone influences) that must be applied
            // before using them to look up a bone. Without this, animation silently freezes because
            // vertices reference the wrong/nonexistent bones (falling back to the static bind pose).
            f.seekg((std::streamoff)boneIndicesOffset, std::ios::beg);
            model.influences.resize(boneIndexCount);
            for (uint32_t i = 0; i < boneIndexCount; ++i) {
                uint16_t idx = 0;
                ReadRaw(f, idx);
                model.influences[i] = idx;
            }
        }
    }

    SklModel SklLoader::Load(const std::string& filePath) {
        SklModel model;

        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return model;

        // Layout: signature(u32, arbitrary/unused) + type(u32: Classic/Version2) + version(u32) + ...
        uint32_t signature = 0;
        if (!ReadRaw(f, signature)) return model;

        uint32_t type = 0;
        if (!ReadRaw(f, type)) return model;

        if (type == kVersion2Signature) {
            ReadVersion2(f, model);
        } else {
            // Classic format: type field doubles as the version marker consumed by ReadClassic.
            f.seekg(4, std::ios::beg);
            ReadClassic(f, model);
        }

        model.valid = !model.bones.empty();
        return model;
    }
}
