// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include "Resource.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>


namespace Resource {

    static std::unordered_map<uint32_t, std::string> ParseResIniData(const std::vector<uint8_t>& data) {
        std::unordered_map<uint32_t, std::string> map;
        if (data.empty()) return map;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;

        while (std::getline(iss, line)) {
            line.erase(0, line.find_first_not_of(" \r\n\t"));
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                try {
                    uint32_t id = std::stoul(line.substr(0, eqPos));
                    std::string path = line.substr(eqPos + 1);
                    for (auto& c : path) if (c == '\\') c = '/';
                    map[id] = path;
                }
                catch (...) {}
            }
        }
        return map;
    }


    static std::unordered_map<std::string, EffectConfig> Parse3DEffectsData(const std::vector<uint8_t>& data) {
        std::unordered_map<std::string, EffectConfig> effects;
        if (data.empty()) return effects;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;

        EffectConfig currentEffect;
        bool inSection = false;

        auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };

        while (std::getline(iss, line)) {
            line.erase(0, line.find_first_not_of(" \r\n\t"));
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[' && line.back() == ']') {
                if (inSection && !currentEffect.name.empty()) effects[currentEffect.name] = currentEffect;
                currentEffect = EffectConfig();
                currentEffect.name = line.substr(1, line.size() - 2);
                inSection = true;
                continue;
            }

            if (!inSection) continue;

            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string val = line.substr(eqPos + 1);

                if (key == "Amount") {
                    currentEffect.amount = safeStoi(val);
                    currentEffect.parts.resize(currentEffect.amount);
                }
                else if (key == "Delay") currentEffect.delay = safeStoi(val);
                else if (key == "LoopTime") currentEffect.loopTime = safeStoi(val);
                else if (key == "FrameInterval") currentEffect.frameInterval = safeStoi(val);
                else if (key == "OffsetX") currentEffect.offsetX = (float)safeStoi(val);
                else if (key == "OffsetY") currentEffect.offsetY = (float)safeStoi(val);
                else if (key == "OffsetZ") currentEffect.offsetZ = (float)safeStoi(val);
                else if (key == "LoopInterval") currentEffect.loopInterval = safeStoi(val);
                else if (key == "ColorEnable") currentEffect.colorEnable = safeStoi(val);
                else if (key.find("EffectId") == 0) {
                    int idx = safeStoi(key.substr(8));
                    if (idx >= 0 && idx < currentEffect.parts.size()) currentEffect.parts[idx].effectId = safeStoi(val);
                }
                else if (key.find("TextureId") == 0) {
                    int idx = safeStoi(key.substr(9));
                    if (idx >= 0 && idx < currentEffect.parts.size()) currentEffect.parts[idx].textureId = safeStoi(val);
                }
                else if (key.find("ASB") == 0 || key.find("Asb") == 0) {
                    int idx = safeStoi(key.substr(3));
                    if (idx >= 0 && idx < currentEffect.parts.size()) currentEffect.parts[idx].asb = safeStoi(val);
                }
                else if (key.find("ADB") == 0 || key.find("Adb") == 0) {
                    int idx = safeStoi(key.substr(3));
                    if (idx >= 0 && idx < currentEffect.parts.size()) currentEffect.parts[idx].adb = safeStoi(val);
                }
            }
        }
        if (inSection && !currentEffect.name.empty()) effects[currentEffect.name] = currentEffect;

        return effects;
    }


    static std::unordered_map<uint32_t, ArmorConfig> ParseArmorIniData(const std::vector<uint8_t>& data) {
        std::unordered_map<uint32_t, ArmorConfig> armors;
        if (data.empty()) return armors;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;

        ArmorConfig currentArmor;
        bool inSection = false;

        auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };
        auto safeStoul = [](const std::string& s) -> uint32_t { try { return std::stoul(s); } catch (...) { return 0; } };

        while (std::getline(iss, line)) {
            line.erase(0, line.find_first_not_of(" \r\n\t"));
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[' && line.back() == ']') {
                if (inSection && currentArmor.id != 0) armors[currentArmor.id] = currentArmor;
                currentArmor = ArmorConfig();
                currentArmor.id = safeStoul(line.substr(1, line.size() - 2));
                inSection = true;
                continue;
            }

            if (!inSection) continue;

            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string val = line.substr(eqPos + 1);

                if (key == "Part") {
                    currentArmor.partCount = safeStoi(val);
                    currentArmor.parts.resize(currentArmor.partCount);
                }
                else if (key.find("Mesh") == 0) {
                    int idx = safeStoi(key.substr(4));
                    if (idx >= 0 && idx < currentArmor.parts.size()) currentArmor.parts[idx].mesh = safeStoul(val);
                }
                else if (key.find("Texture") == 0) {
                    int idx = safeStoi(key.substr(7));
                    if (idx >= 0 && idx < currentArmor.parts.size()) currentArmor.parts[idx].texture = safeStoul(val);
                }
                else if (key.find("Asb") == 0 || key.find("ASB") == 0) {
                    int idx = safeStoi(key.substr(3));
                    if (idx >= 0 && idx < currentArmor.parts.size()) currentArmor.parts[idx].asb = safeStoi(val);
                }
                else if (key.find("Adb") == 0 || key.find("ADB") == 0) {
                    int idx = safeStoi(key.substr(3));
                    if (idx >= 0 && idx < currentArmor.parts.size()) currentArmor.parts[idx].adb = safeStoi(val);
                }
            }
        }
        if (inSection && currentArmor.id != 0) armors[currentArmor.id] = currentArmor;

        return armors;
    }
}