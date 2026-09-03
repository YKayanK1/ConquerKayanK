#include "pch.h"
#include "AnmLoader.h"
#include <fstream>
#include <cstring>
#include <unordered_map>

namespace Riot {

    namespace {
        template<typename T>
        bool ReadRaw(std::ifstream& f, T& out) {
            f.read(reinterpret_cast<char*>(&out), sizeof(T));
            return (bool)f;
        }

        bool MatchTag(const char tag[8], const char* expected) {
            return std::memcmp(tag, expected, 8) == 0;
        }

        // Decompresses a quaternion packed into 48 bits (6 bytes), used by .anm v1/v4 compressed tracks.
        XMFLOAT4 UncompressQuaternion(uint64_t bits) {
            // Layout: 3 bits selecting the dropped (largest) component, then three 15-bit signed fields.
            const double kSqrt2 = 1.41421356237;
            int maxIndex = (int)(bits >> 45) & 0x3;
            int cx = (int)(bits >> 30) & 0x7FFF;
            int cy = (int)(bits >> 15) & 0x7FFF;
            int cz = (int)(bits) & 0x7FFF;

            auto decode = [&](int v) {
                double normalized = ((double)v / 32767.0) * 2.0 - 1.0;
                return normalized / kSqrt2;
                };

            double a = decode(cx);
            double b = decode(cy);
            double c = decode(cz);
            double d = 1.0 - (a * a + b * b + c * c);
            if (d < 0.0) d = 0.0;
            d = sqrt(d);

            double q[4] = { a, b, c, d };
            // Rotate the dropped component back into its original slot.
            double result[4] = { 0,0,0,0 };
            int w = 0;
            for (int i = 0; i < 4; ++i) {
                if (i == maxIndex) { result[i] = q[3]; }
                else { result[i] = q[w++]; }
            }

            return XMFLOAT4((float)result[0], (float)result[1], (float)result[2], (float)result[3]);
        }

        XMFLOAT3 UncompressVec3(const XMFLOAT3& minV, const XMFLOAT3& maxV, uint16_t x, uint16_t y, uint16_t z) {
            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            float tx = (float)x / 65535.0f;
            float ty = (float)y / 65535.0f;
            float tz = (float)z / 65535.0f;
            return XMFLOAT3(lerp(minV.x, maxV.x, tx), lerp(minV.y, maxV.y, ty), lerp(minV.z, maxV.z, tz));
        }

        float UncompressTime(uint16_t t, float duration) {
            return ((float)t / 65535.0f) * duration;
        }

        AnmBoneTrack& GetOrCreateTrack(AnmModel& model, uint32_t hash, std::unordered_map<uint32_t, size_t>& index) {
            auto it = index.find(hash);
            if (it != index.end()) return model.tracks[it->second];
            AnmBoneTrack track;
            track.boneNameHash = hash;
            model.tracks.push_back(track);
            index[hash] = model.tracks.size() - 1;
            return model.tracks.back();
        }

        // Version 3: uncompressed, per-frame full transform for every bone (no time curve compression).
        void LoadVersion3(std::ifstream& f, AnmModel& model) {
            uint32_t boneCount = 0, frameCount = 0;
            ReadRaw(f, boneCount);
            ReadRaw(f, frameCount);

            float fps = 30.0f;
            ReadRaw(f, fps);
            model.fps = fps;
            model.duration = frameCount > 0 ? (float)(frameCount - 1) / fps : 0.0f;

            std::unordered_map<uint32_t, size_t> index;

            for (uint32_t bi = 0; bi < boneCount; ++bi) {
                uint32_t nameHash = 0;
                ReadRaw(f, nameHash);

                AnmBoneTrack& track = GetOrCreateTrack(model, nameHash, index);

                for (uint32_t fr = 0; fr < frameCount; ++fr) {
                    XMFLOAT4 rot{};
                    XMFLOAT3 pos{};
                    ReadRaw(f, rot);
                    ReadRaw(f, pos);

                    float time = (float)fr / fps;
                    track.rotationFrames.push_back({ time, rot });
                    track.translationFrames.push_back({ time, pos });
                }
            }
        }

        // Legacy uncompressed format used by "r3d2anmd" files whose version is neither 4 nor 5
        // (this includes files tagged with version==1 and other legacy numbers). Each bone is
        // stored as a 32-byte name (not a hash!) followed by a 4-byte padding, then a full
        // rotation+translation pair per frame (no compression, no per-bone frame count table).
        void LoadLegacy(std::ifstream& f, AnmModel& model) {
            f.seekg(4, std::ios::cur); // unused field right after version

            uint32_t boneCount = 0, frameCount = 0;
            ReadRaw(f, boneCount);
            ReadRaw(f, frameCount);

            int32_t fps32 = 30;
            ReadRaw(f, fps32);
            float fps = (float)fps32;
            model.fps = fps;
            model.duration = frameCount * (1.0f / fps);

            std::unordered_map<uint32_t, size_t> index;

            for (uint32_t bi = 0; bi < boneCount; ++bi) {
                char nameBuf[32] = {};
                f.read(nameBuf, sizeof(nameBuf));
                std::string name(nameBuf, strnlen(nameBuf, sizeof(nameBuf)));
                uint32_t nameHash = ElfHash(name);

                f.seekg(4, std::ios::cur); // unused padding after name

                AnmBoneTrack& track = GetOrCreateTrack(model, nameHash, index);

                float currentTime = 0.0f;
                for (uint32_t fr = 0; fr < frameCount; ++fr) {
                    XMFLOAT4 rot{};
                    XMFLOAT3 pos{};
                    ReadRaw(f, rot);
                    ReadRaw(f, pos);

                    track.rotationFrames.push_back({ currentTime, rot });
                    track.translationFrames.push_back({ currentTime, pos });

                    currentTime += (1.0f / fps);
                }
            }
        }

        // Compressed keyframes stored in a single global entry table (per-frame-type,
        // referencing bones by index into a hash table), not a sequential per-bone layout.
        // Used only by files tagged "r3d2canm". Header has bounding boxes for translation/scale,
        // followed by entriesOffset/indicesOffset/hashesOffset (all relative to just after the
        // 12-byte tag+version header), a hash table, and a flat array of 10-byte compressed entries.
        void LoadCompressed(std::ifstream& f, AnmModel& model) {
            int32_t fileSize = 0;
            ReadRaw(f, fileSize);

            f.seekg(8, std::ios::cur); // unused/reserved fields

            uint32_t boneCount = 0;
            int32_t entryCount = 0;
            ReadRaw(f, boneCount);
            ReadRaw(f, entryCount);

            f.seekg(4, std::ios::cur); // unused field

            float duration = 0.0f, fps = 30.0f;
            ReadRaw(f, duration);
            ReadRaw(f, fps);
            model.fps = fps;
            model.duration = duration;

            f.seekg(24, std::ios::cur); // unused padding

            XMFLOAT3 translationMin{}, translationMax{}, scaleMin{}, scaleMax{};
            ReadRaw(f, translationMin);
            ReadRaw(f, translationMax);
            ReadRaw(f, scaleMin);
            ReadRaw(f, scaleMax);

            uint32_t entriesOffset = 0, indicesOffset = 0, hashesOffset = 0;
            ReadRaw(f, entriesOffset);
            ReadRaw(f, indicesOffset);
            ReadRaw(f, hashesOffset);

            entriesOffset += 12;
            indicesOffset += 12;
            hashesOffset += 12;
            (void)indicesOffset; // read from file but unused by the reference parser

            std::vector<uint32_t> hashEntries(boneCount);
            f.seekg((std::streamoff)hashesOffset, std::ios::beg);
            for (uint32_t i = 0; i < boneCount; ++i) {
                ReadRaw(f, hashEntries[i]);
            }

            enum FrameDataType : uint8_t {
                RotationType = 0,
                TranslationType = 64,
                ScaleType = 128
            };

            std::vector<std::vector<std::pair<uint16_t, uint64_t>>> compressedRotations(boneCount);
            std::vector<std::vector<std::pair<uint16_t, uint64_t>>> compressedTranslations(boneCount);
            std::vector<std::vector<std::pair<uint16_t, uint64_t>>> compressedScales(boneCount);

            f.seekg((std::streamoff)entriesOffset, std::ios::beg);
            for (int32_t i = 0; i < entryCount; ++i) {
                uint16_t compressedTime = 0;
                ReadRaw(f, compressedTime);

                uint8_t hashIndex = 0;
                ReadRaw(f, hashIndex);

                uint8_t dataType = 0;
                ReadRaw(f, dataType);

                uint64_t compressedData = 0;
                f.read(reinterpret_cast<char*>(&compressedData), 6);

                if (hashIndex >= boneCount) continue;

                switch (dataType) {
                    case RotationType:
                        compressedRotations[hashIndex].push_back({ compressedTime, compressedData });
                        break;
                    case TranslationType:
                        compressedTranslations[hashIndex].push_back({ compressedTime, compressedData });
                        break;
                    case ScaleType:
                        compressedScales[hashIndex].push_back({ compressedTime, compressedData });
                        break;
                    default:
                        break;
                }
            }

            std::unordered_map<uint32_t, size_t> index;
            for (uint32_t i = 0; i < boneCount; ++i) {
                uint32_t boneHash = hashEntries[i];
                AnmBoneTrack& track = GetOrCreateTrack(model, boneHash, index);

                for (auto& entry : compressedTranslations[i]) {
                    float time = UncompressTime(entry.first, model.duration);
                    uint16_t x = (uint16_t)(entry.second & 0xFFFF);
                    uint16_t y = (uint16_t)((entry.second >> 16) & 0xFFFF);
                    uint16_t z = (uint16_t)((entry.second >> 32) & 0xFFFF);
                    XMFLOAT3 pos = UncompressVec3(translationMin, translationMax, x, y, z);
                    track.translationFrames.push_back({ time, pos });
                }

                for (auto& entry : compressedRotations[i]) {
                    float time = UncompressTime(entry.first, model.duration);
                    XMFLOAT4 rot = UncompressQuaternion(entry.second);
                    track.rotationFrames.push_back({ time, rot });
                }

                for (auto& entry : compressedScales[i]) {
                    float time = UncompressTime(entry.first, model.duration);
                    uint16_t x = (uint16_t)(entry.second & 0xFFFF);
                    uint16_t y = (uint16_t)((entry.second >> 16) & 0xFFFF);
                    uint16_t z = (uint16_t)((entry.second >> 32) & 0xFFFF);
                    XMFLOAT3 scale = UncompressVec3(scaleMin, scaleMax, x, y, z);
                    track.scaleFrames.push_back({ time, scale });
                }
            }
        }

        // Version 4: uncompressed translation/rotation value tables shared across bones, referenced
        // per-frame-per-bone by 16-bit indices. Matches the real Riot v4 .anm layout (no bounding-volume
        // compression here; that only applies to v5's rotation bitset table).
        void LoadVersion4(std::ifstream& f, AnmModel& model) {
            f.seekg(16, std::ios::cur);

            uint32_t boneCount = 0, frameCount = 0;
            float frameDelay = 0.0f;
            ReadRaw(f, boneCount);
            ReadRaw(f, frameCount);
            ReadRaw(f, frameDelay);
            f.seekg(12, std::ios::cur);

            model.duration = frameDelay * frameCount;
            model.fps = frameDelay > 0.0f ? (1.0f / frameDelay) : 30.0f;

            uint32_t translationDataOffset = 0, rotationDataOffset = 0, frameDataOffset = 0;
            ReadRaw(f, translationDataOffset); translationDataOffset += 12;
            ReadRaw(f, rotationDataOffset); rotationDataOffset += 12;
            ReadRaw(f, frameDataOffset); frameDataOffset += 12;

            std::vector<XMFLOAT3> translationOrScaleEntries;
            f.seekg(translationDataOffset, std::ios::beg);
            for (uint32_t off = translationDataOffset; off < rotationDataOffset; off += 12) {
                XMFLOAT3 vec{};
                ReadRaw(f, vec.x); ReadRaw(f, vec.y); ReadRaw(f, vec.z);
                translationOrScaleEntries.push_back(vec);
            }

            std::vector<XMFLOAT4> rotationEntries;
            f.seekg(rotationDataOffset, std::ios::beg);
            for (uint32_t off = rotationDataOffset; off < frameDataOffset; off += 16) {
                XMFLOAT4 rot{};
                ReadRaw(f, rot.x); ReadRaw(f, rot.y); ReadRaw(f, rot.z); ReadRaw(f, rot.w);
                rotationEntries.push_back(rot);
            }

            struct FrameIndices { uint16_t translationIndex; uint16_t scaleIndex; uint16_t rotationIndex; };
            std::unordered_map<uint32_t, std::vector<FrameIndices>> boneMap;
            std::vector<uint32_t> boneOrder;

            f.seekg(frameDataOffset, std::ios::beg);
            for (uint32_t i = 0; i < boneCount; ++i) {
                for (uint32_t j = 0; j < frameCount; ++j) {
                    uint32_t boneHash = 0;
                    FrameIndices fi{};
                    ReadRaw(f, boneHash);
                    ReadRaw(f, fi.translationIndex);
                    ReadRaw(f, fi.scaleIndex);
                    ReadRaw(f, fi.rotationIndex);
                    f.seekg(2, std::ios::cur);

                    auto it = boneMap.find(boneHash);
                    if (it == boneMap.end()) {
                        boneOrder.push_back(boneHash);
                        boneMap[boneHash] = { fi };
                    } else {
                        it->second.push_back(fi);
                    }
                }
            }

            std::unordered_map<uint32_t, size_t> index;
            for (uint32_t hash : boneOrder) {
                AnmBoneTrack& track = GetOrCreateTrack(model, hash, index);
                float currentTime = 0.0f;
                for (const auto& fi : boneMap[hash]) {
                    if (fi.translationIndex < translationOrScaleEntries.size())
                        track.translationFrames.push_back({ currentTime, translationOrScaleEntries[fi.translationIndex] });
                    if (fi.rotationIndex < rotationEntries.size())
                        track.rotationFrames.push_back({ currentTime, rotationEntries[fi.rotationIndex] });
                    if (fi.scaleIndex < translationOrScaleEntries.size())
                        track.scaleFrames.push_back({ currentTime, translationOrScaleEntries[fi.scaleIndex] });
                    currentTime += frameDelay;
                }
            }
        }

        // Version 5: shared value tables (translation/scale share one array, rotation another),
        // referenced by a frame-major table of frameCount * jointCount entries (translationIndex,
        // scaleIndex, rotationIndex per joint per frame), matching the real Riot v5 .anm layout.
        void LoadVersion5(std::ifstream& f, AnmModel& model) {
            f.seekg(16, std::ios::cur); // resourceSize/formatToken/flags/extra (16 bytes total per reference)

            uint32_t jointCount = 0, frameCount = 0;
            ReadRaw(f, jointCount);
            ReadRaw(f, frameCount);

            float frameDelay = 0.0f;
            ReadRaw(f, frameDelay);
            model.duration = frameCount * frameDelay;
            model.fps = frameDelay > 0.0f ? (frameCount / model.duration) : 30.0f;

            uint32_t hashesOffset = 0;
            ReadRaw(f, hashesOffset);
            f.seekg(8, std::ios::cur);

            uint32_t vectorsOffset = 0, rotationsOffset = 0, frameOffset = 0;
            ReadRaw(f, vectorsOffset);
            ReadRaw(f, rotationsOffset);
            ReadRaw(f, frameOffset);

            if (jointCount == 0 || frameCount == 0) {
                model.fps = 30.0f;
                return;
            }

            uint32_t hashesCount = (frameOffset - hashesOffset) / 4;
            uint32_t vectorsCount = (rotationsOffset - vectorsOffset) / 12;
            uint32_t rotationsCount = (hashesOffset - rotationsOffset) / 6;

            f.seekg(hashesOffset + 12, std::ios::beg);
            std::vector<uint32_t> hashes(hashesCount);
            for (uint32_t i = 0; i < hashesCount; ++i) ReadRaw(f, hashes[i]);

            f.seekg(vectorsOffset + 12, std::ios::beg);
            std::vector<XMFLOAT3> vectors(vectorsCount);
            for (uint32_t i = 0; i < vectorsCount; ++i) ReadRaw(f, vectors[i]);

            f.seekg(rotationsOffset + 12, std::ios::beg);
            std::vector<uint64_t> rotations(rotationsCount);
            for (uint32_t i = 0; i < rotationsCount; ++i) {
                uint64_t bits = 0;
                f.read(reinterpret_cast<char*>(&bits), 6);
                rotations[i] = bits;
            }

            std::unordered_map<uint32_t, size_t> index;
            std::vector<size_t> trackIndices(jointCount, (size_t)-1);
            for (uint32_t i = 0; i < jointCount && i < hashesCount; ++i) {
                GetOrCreateTrack(model, hashes[i], index);
                trackIndices[i] = index[hashes[i]];
            }

            f.seekg(frameOffset + 12, std::ios::beg);
            float currentTime = 0.0f;
            for (uint32_t fr = 0; fr < frameCount; ++fr) {
                for (uint32_t j = 0; j < jointCount; ++j) {
                    uint16_t translationIndex = 0, scaleIndex = 0, rotationIndex = 0;
                    ReadRaw(f, translationIndex);
                    ReadRaw(f, scaleIndex);
                    ReadRaw(f, rotationIndex);

                    if (trackIndices[j] == (size_t)-1) continue;
                    AnmBoneTrack& track = model.tracks[trackIndices[j]];

                    if (rotationIndex < rotations.size()) {
                        XMFLOAT4 rot = UncompressQuaternion(rotations[rotationIndex]);
                        track.rotationFrames.push_back({ currentTime, rot });
                    }
                    if (scaleIndex < vectors.size()) {
                        track.scaleFrames.push_back({ currentTime, vectors[scaleIndex] });
                    }
                    if (translationIndex < vectors.size()) {
                        track.translationFrames.push_back({ currentTime, vectors[translationIndex] });
                    }
                }
                currentTime += frameDelay;
            }
        }
    }

    AnmModel AnmLoader::Load(const std::string& filePath) {
        AnmModel model;

        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return model;

        char tag[8] = {};
        f.read(tag, sizeof(tag));

        bool isCompressed = MatchTag(tag, "r3d2canm");
        bool isUncompressed = MatchTag(tag, "r3d2anmd");

        if (!isCompressed && !isUncompressed) return model;

        uint32_t version = 0;
        ReadRaw(f, version);

        // The tag is authoritative for choosing the compressed vs. uncompressed family, matching
        // the reference parser: "r3d2canm" always uses the compressed layout regardless of its
        // version field, while "r3d2anmd" dispatches by version (5/4 have dedicated layouts,
        // anything else falls back to the simple legacy per-frame layout).
        if (isCompressed) {
            LoadCompressed(f, model);
        } else {
            switch (version) {
                case 5: LoadVersion5(f, model); break;
                case 4: LoadVersion4(f, model); break;
                default: LoadLegacy(f, model); break;
            }
        }

        model.valid = !model.tracks.empty();

        {
            char dbg[256];
            sprintf_s(dbg, "[Riot][Anm] file=%s tag=%.8s version=%u tracks=%zu valid=%d\n",
                filePath.c_str(), tag, version, model.tracks.size(), model.valid ? 1 : 0);
            OutputDebugStringA(dbg);
        }

        return model;
    }
}
