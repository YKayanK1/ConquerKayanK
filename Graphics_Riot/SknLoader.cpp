#include "pch.h"
#include "SknLoader.h"
#include <fstream>
#include <cstring>

namespace Riot {

    namespace {
        template<typename T>
        bool ReadRaw(std::ifstream& f, T& out) {
            f.read(reinterpret_cast<char*>(&out), sizeof(T));
            return (bool)f;
        }

        template<typename T>
        bool ReadArray(std::ifstream& f, std::vector<T>& out, size_t count) {
            out.resize(count);
            if (count == 0) return true;
            f.read(reinterpret_cast<char*>(out.data()), sizeof(T) * count);
            return (bool)f;
        }
    }

    SknModel SknLoader::Load(const std::string& filePath) {
        SknModel model;

        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return model;

        uint32_t signature = 0;
        if (!ReadRaw(f, signature)) return model;
        if (signature != 0x00112233u) return model;

        uint16_t majorVersion = 0, minorVersion = 0;
        ReadRaw(f, majorVersion);
        ReadRaw(f, minorVersion);

        if (majorVersion > 4) return model;

        if (majorVersion == 0) {
            // Very old version: no submesh table, index/vertex counts follow directly.
            uint32_t indexCount = 0, vertexCount = 0;
            ReadRaw(f, indexCount);
            ReadRaw(f, vertexCount);

            SknSubMesh sm;
            sm.name = "Base";
            sm.vertexOffset = 0;
            sm.vertexCount = vertexCount;
            sm.indexOffset = 0;
            sm.indexCount = indexCount;
            model.subMeshes.push_back(sm);

            ReadArray(f, model.indices, indexCount);

            model.vertices.resize(vertexCount);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                SknVertex v{};
                ReadRaw(f, v.position);
                ReadRaw(f, v.boneIndices);
                ReadRaw(f, v.weights);
                ReadRaw(f, v.normal);
                ReadRaw(f, v.uv);
                model.vertices[i] = v;
            }

            model.valid = f.good() || f.eof();
            return model;
        }

        uint32_t subMeshCount = 0;
        ReadRaw(f, subMeshCount);

        struct RawSubMeshHeader {
            char name[64];
            uint32_t vertexOffset;
            uint32_t vertexCount;
            uint32_t indexOffset;
            uint32_t indexCount;
        };

        std::vector<RawSubMeshHeader> rawHeaders(subMeshCount);
        for (uint32_t i = 0; i < subMeshCount; ++i) {
            f.read(rawHeaders[i].name, sizeof(rawHeaders[i].name));
            ReadRaw(f, rawHeaders[i].vertexOffset);
            ReadRaw(f, rawHeaders[i].vertexCount);
            ReadRaw(f, rawHeaders[i].indexOffset);
            ReadRaw(f, rawHeaders[i].indexCount);
        }

        // Version 4 adds an extra flag block right after the submesh headers.
        uint32_t extraFlag = 0;
        if (majorVersion == 4) {
            ReadRaw(f, extraFlag);
        }

        uint32_t indexCount = 0, vertexCount = 0;
        ReadRaw(f, indexCount);
        ReadRaw(f, vertexCount);

        // Only AFTER indexCount/vertexCount does version 4 store vertexType + bounding volume data.
        uint32_t vertexType = 0;
        float boundingBoxMin[3] = {}, boundingBoxMax[3] = {};
        if (majorVersion == 4) {
            uint32_t unused = 0;
            ReadRaw(f, unused);
            ReadRaw(f, vertexType);
            ReadRaw(f, boundingBoxMin);
            ReadRaw(f, boundingBoxMax);
            f.seekg(16, std::ios::cur);
        }

        ReadArray(f, model.indices, indexCount);

        model.vertices.resize(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            SknVertex v{};
            ReadRaw(f, v.position);
            ReadRaw(f, v.boneIndices);
            ReadRaw(f, v.weights);
            ReadRaw(f, v.normal);
            ReadRaw(f, v.uv);
            // Reference parser only skips a single extra 4-byte field when vertexType > 0
            // (regardless of whether it's color or tangent data), never two extra fields.
            if (vertexType > 0) {
                uint8_t extra[4];
                ReadRaw(f, extra);
            }
            model.vertices[i] = v;
        }

        model.subMeshes.resize(subMeshCount);
        for (uint32_t i = 0; i < subMeshCount; ++i) {
            SknSubMesh sm;
            sm.name = std::string(rawHeaders[i].name);
            sm.vertexOffset = rawHeaders[i].vertexOffset;
            sm.vertexCount = rawHeaders[i].vertexCount;
            sm.indexOffset = rawHeaders[i].indexOffset;
            sm.indexCount = rawHeaders[i].indexCount;
            model.subMeshes[i] = sm;
        }

        if (model.subMeshes.empty()) {
            SknSubMesh sm;
            sm.name = "Base";
            sm.vertexOffset = 0;
            sm.vertexCount = vertexCount;
            sm.indexOffset = 0;
            sm.indexCount = indexCount;
            model.subMeshes.push_back(sm);
        }

        model.valid = true;
        return model;
    }
}
