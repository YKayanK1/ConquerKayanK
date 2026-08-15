// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#include "pch.h"
#define NOMINMAX
#include "Resource.h"
#include "Resource_Utils.h"
#include "Resource_C3.h"
#include "Resource_Map.h"
#include "Resource_Effect.h" 

#include <fstream>
#include <iostream>
#include <filesystem>
#include <memory>
#include <unordered_map>


namespace Resource {

    struct PackedFile { uint32_t fileId, offset, size, reserved; };

    struct WdfPackage {
        std::ifstream stream;
        std::unordered_map<uint32_t, PackedFile> index;
    };

    struct Manager::Impl {
        std::string clientPath;
        std::vector<std::shared_ptr<WdfPackage>> m_packages;

        bool Initialize(const std::string& path) {
            clientPath = path;
            if (!clientPath.empty() && clientPath.back() != '\\' && clientPath.back() != '/') {
                clientPath += "\\";
            }

            auto loadWdf = [&](const std::string& relativePkgPath) {
                std::string wdfPath = clientPath + relativePkgPath;
                auto pkg = std::make_shared<WdfPackage>();
                pkg->stream.open(wdfPath, std::ios::binary);
                if (pkg->stream.is_open()) {
                    uint32_t magic, fileCount, indexOffset;
                    pkg->stream.read((char*)&magic, 4);
                    pkg->stream.read((char*)&fileCount, 4);
                    pkg->stream.read((char*)&indexOffset, 4);
                    pkg->stream.seekg(indexOffset);
                    for (uint32_t i = 0; i < fileCount; i++) {
                        PackedFile pf;
                        pkg->stream.read((char*)&pf, 16);
                        pkg->index[pf.fileId] = pf;
                    }
                    m_packages.push_back(pkg);
                    std::cout << "[LOG] WDF Package Reader (" << relativePkgPath << ") ativado! Indexados " << fileCount << " arquivos.\n";
                }
                };

            for (const auto& entry : std::filesystem::directory_iterator(clientPath)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    for (auto& c : ext) c = std::tolower(c);
                    if (ext == ".wdf") loadWdf(entry.path().filename().string());
                }
            }

            std::string dataPath = clientPath + "data\\";
            if (std::filesystem::exists(dataPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(dataPath)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        for (auto& c : ext) c = std::tolower(c);
                        if (ext == ".wdf") loadWdf("data\\" + entry.path().filename().string());
                    }
                }
            }

            return std::filesystem::exists(clientPath);
        }

        std::vector<uint8_t> ReadLooseFile(const std::string& relativePath) {
            std::string fullPath = clientPath + relativePath;
            std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) return {};
            size_t size = (size_t)file.tellg(); file.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(size);
            if (file.read(reinterpret_cast<char*>(buffer.data()), size)) return buffer;
            return {};
        }

        std::vector<uint8_t> GetFileData(const std::string& internalPath) {
            auto loose = ReadLooseFile(internalPath);
            if (!loose.empty()) return loose;

            uint32_t hash = HashFilename(internalPath);
            for (auto& pkg : m_packages) {
                if (pkg->index.count(hash)) {
                    auto& pf = pkg->index[hash];
                    pkg->stream.clear();
                    pkg->stream.seekg(pf.offset, std::ios::beg);
                    std::vector<uint8_t> data(pf.size);
                    pkg->stream.read((char*)data.data(), pf.size);
                    return data;
                }
            }
            return {};
        }


        C3Model LoadC3Model(const std::string& c3Path) {
            return ParseC3ModelData(GetFileData(c3Path), c3Path);
        }

        std::unordered_map<uint32_t, GameMapRecord> LoadGameMapDat(const std::string& path) {
            return ParseGameMapDatData(GetFileData(path));
        }

        DMapData LoadDMap(const std::string& path) {
            return ParseDMapData(GetFileData(path));
        }

        PulData LoadPul(const std::string& path) {
            return ParsePulData(GetFileData(path));
        }

        std::string ParseAniSection(const std::string& aniPath, const std::string& sectionName) {
            return ParseAniSectionData(GetFileData(aniPath), sectionName);
        }

        std::unordered_map<std::string, EffectConfig> Parse3DEffects(const std::string& path) {
            return Parse3DEffectsData(GetFileData(path));
        }

        std::unordered_map<uint32_t, std::string> ParseResIni(const std::string& path) {
            return ParseResIniData(GetFileData(path));
        }

        std::unordered_map<uint32_t, ArmorConfig> ParseArmorIni(const std::string& path) {
            return ParseArmorIniData(GetFileData(path));
        }

        // [NOVO] 
        std::unordered_map<uint32_t, WeaponConfig> ParseWeaponIni(const std::string& path) {
            return ParseWeaponIniData(GetFileData(path));
        }
    };

    Manager::Manager() : pImpl(new Impl()) {}
    Manager::~Manager() { delete pImpl; }

    bool Manager::Initialize(const std::string& cp) { return pImpl->Initialize(cp); }
    std::vector<uint8_t> Manager::ReadLooseFile(const std::string& rp) { return pImpl->ReadLooseFile(rp); }
    std::vector<uint8_t> Manager::GetFileData(const std::string& ip) { return pImpl->GetFileData(ip); }
    C3Model Manager::LoadC3Model(const std::string& c3) { return pImpl->LoadC3Model(c3); }
    DMapData Manager::LoadDMap(const std::string& path) { return pImpl->LoadDMap(path); }
    PulData Manager::LoadPul(const std::string& path) { return pImpl->LoadPul(path); }
    std::unordered_map<uint32_t, GameMapRecord> Manager::LoadGameMapDat(const std::string& p) { return pImpl->LoadGameMapDat(p); }
    std::string Manager::ParseAniSection(const std::string& ani, const std::string& sec) { return pImpl->ParseAniSection(ani, sec); }

    std::unordered_map<std::string, EffectConfig> Manager::Parse3DEffects(const std::string& path) { return pImpl->Parse3DEffects(path); }
    std::unordered_map<uint32_t, std::string> Manager::ParseResIni(const std::string& path) { return pImpl->ParseResIni(path); }
    std::unordered_map<uint32_t, ArmorConfig> Manager::ParseArmorIni(const std::string& path) { return pImpl->ParseArmorIni(path); }
    std::unordered_map<uint32_t, WeaponConfig> Manager::ParseWeaponIni(const std::string& path) { return pImpl->ParseWeaponIni(path); } // [NOVO]
}