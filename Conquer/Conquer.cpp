// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib> 
#include <ctime>   

#include "../Graphics/Graphics.h"
#include "../Resource/Resource.h"
#include "Engine_Window.h"
#include "Engine_Math.h"
#include "Game_Entities.h"
#include "Game_Utils.h"

#include <DirectXMath.h>

namespace Game {
    std::tuple<int, int, int> GenerateGlowTextTexture(
        Graphics::SceneRenderer& renderer,
        const std::wstring& text,
        COLORREF textColor = RGB(255, 255, 255),
        COLORREF glowColor = RGB(120, 140, 255),
        int glowRadius = 10,
        int fontSize = 32
    ) {
        HDC hdc = CreateCompatibleDC(NULL);
        HFONT hFont = CreateFontW(
            fontSize, 0, 0, 0, FW_EXTRABOLD, TRUE, TRUE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Arial"
        );
        SelectObject(hdc, hFont);

        SIZE size;
        GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &size);

        int padding = glowRadius + 4;
        int width = size.cx + padding * 2;
        int height = size.cy + padding * 2;

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        SelectObject(hdc, hBitmap);

        memset(bits, 0, width * height * 4);

        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        TextOutW(hdc, padding, padding, text.c_str(), (int)text.length());

        uint8_t* rawPixels = (uint8_t*)bits;

        std::vector<float> glyphMask(width * height, 0.0f);
        for (int i = 0; i < width * height; i++) {
            uint8_t r = rawPixels[i * 4 + 2];
            uint8_t g = rawPixels[i * 4 + 1];
            uint8_t b = rawPixels[i * 4 + 0];
            float maxVal = (float)(std::max)({ r, g, b });
            glyphMask[i] = maxVal / 255.0f;
        }

        std::vector<float> glowMask(width * height, 0.0f);
        float glowR = (float)(glowColor & 0xFF);
        float glowG = (float)((glowColor >> 8) & 0xFF);
        float glowB = (float)((glowColor >> 16) & 0xFF);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float maxGlowVal = 0.0f;
                for (int dy = -glowRadius; dy <= glowRadius; dy++) {
                    int ny = y + dy;
                    if (ny < 0 || ny >= height) continue;
                    for (int dx = -glowRadius; dx <= glowRadius; dx++) {
                        int nx = x + dx;
                        if (nx < 0 || nx >= width) continue;
                        float srcVal = glyphMask[ny * width + nx];
                        if (srcVal > 0.01f) {
                            float dist = std::sqrt((float)(dx * dx + dy * dy));
                            if (dist <= glowRadius) {
                                float factor = 1.0f - (dist / (float)glowRadius);
                                float glowVal = srcVal * (factor * factor);
                                if (glowVal > maxGlowVal) maxGlowVal = glowVal;
                            }
                        }
                    }
                }
                glowMask[y * width + x] = maxGlowVal;
            }
        }

        std::vector<uint8_t> finalPixels(width * height * 4, 0);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                float textVal = glyphMask[idx];
                float auraVal = glowMask[idx];

                float outR = glowR * auraVal;
                float outG = glowG * auraVal;
                float outB = glowB * auraVal;
                float outA = auraVal * 0.90f;

                if (textVal > 0.0f) {
                    float textR = (float)(textColor & 0xFF);
                    float textG = (float)((textColor >> 8) & 0xFF);
                    float textB = (float)((textColor >> 16) & 0xFF);
                    outR = outR * (1.0f - textVal) + textR * textVal;
                    outG = outG * (1.0f - textVal) + textG * textVal;
                    outB = outB * (1.0f - textVal) + textB * textVal;
                    outA = (std::max)(outA, textVal);
                }

                int pIdx = idx * 4;
                finalPixels[pIdx + 0] = (uint8_t)(std::min)(255.0f, outB);
                finalPixels[pIdx + 1] = (uint8_t)(std::min)(255.0f, outG);
                finalPixels[pIdx + 2] = (uint8_t)(std::min)(255.0f, outR);
                finalPixels[pIdx + 3] = (uint8_t)(std::min)(255.0f, outA * 255.0f);
            }
        }

        struct DDS_HEADER {
            uint32_t dwMagic; uint32_t dwSize; uint32_t dwFlags;
            uint32_t dwHeight; uint32_t dwWidth; uint32_t dwPitchOrLinearSize;
            uint32_t dwDepth; uint32_t dwMipMapCount; uint32_t dwReserved1[11];
            struct {
                uint32_t dwSize; uint32_t dwFlags; uint32_t dwFourCC;
                uint32_t dwRGBBitCount; uint32_t dwRBitMask; uint32_t dwGBitMask;
                uint32_t dwBBitMask; uint32_t dwABitMask;
            } ddspf;
            uint32_t dwCaps; uint32_t dwCaps2; uint32_t dwCaps3; uint32_t dwCaps4; uint32_t dwReserved2;
        };

        DDS_HEADER header = { 0 };
        header.dwMagic = 0x20534444;
        header.dwSize = 124;
        header.dwFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
        header.dwHeight = height; header.dwWidth = width;
        header.dwPitchOrLinearSize = width * 4;
        header.ddspf.dwSize = 32; header.ddspf.dwFlags = 0x41;
        header.ddspf.dwRGBBitCount = 32;
        header.ddspf.dwRBitMask = 0x00FF0000; header.ddspf.dwGBitMask = 0x0000FF00;
        header.ddspf.dwBBitMask = 0x000000FF; header.ddspf.dwABitMask = 0xFF000000;
        header.dwCaps = 0x1000;

        std::vector<uint8_t> ddsFile(sizeof(DDS_HEADER) + finalPixels.size());
        memcpy(ddsFile.data(), &header, sizeof(DDS_HEADER));
        memcpy(ddsFile.data() + sizeof(DDS_HEADER), finalPixels.data(), finalPixels.size());

        int texId = renderer.LoadTextureFromMemory(ddsFile.data(), ddsFile.size());

        DeleteObject(hBitmap); DeleteObject(hFont); DeleteDC(hdc);

        return { texId, width, height };
    }
}

class Application {
public:
    std::string m_currentEffectName = "Nenhum";

    Engine::WindowManager m_window;
    Graphics::SceneRenderer m_renderer;
    Resource::Manager m_resource;

    Game::PlayerEntity m_player;
    std::vector<Game::MonsterEntity> m_monsters;
    std::vector<Game::SceneObject> m_sceneObjects;

    Resource::C3Model m_monsterIdleModel, m_monsterWalkModel;
    Resource::C3Model m_monsterDieModel, m_monsterDeadModel;

    Resource::C3Model m_hairIdleModel, m_hairWalkModel, m_hairJumpModel, m_hairAlertModel;
    Resource::C3Model m_hairAttackModel[3];

    Resource::C3Model m_weaponIdleModel;
    Resource::C3Model m_lweaponIdleModel;

    int m_monsterTextureId = -1;
    int m_hairTextureId = -1, m_weaponTextureId = -1, m_lweaponTextureId = -1;

    std::vector<std::string> m_effectList = { "gamebow", "accession", "_p_24_wing1_open110", "blza", "fire", "ice1", "light", "water", "wind", "thunder", "fastbladen", "fbo" };
    int m_currentEffectIndex = -1;

    std::unordered_map<std::string, Resource::EffectConfig> m_effectConfigs;
    std::unordered_map<uint32_t, std::string> m_c3Paths;
    std::unordered_map<uint32_t, std::string> m_ddsPaths;
    std::unordered_map<uint32_t, std::string> m_motionPaths;
    std::unordered_map<uint32_t, Resource::ArmorConfig> m_armorConfigs;

    struct LoadedEffectPart {
        Resource::C3Model model;
        int textureId = -1;
    };

    struct ActiveEffect {
        Resource::EffectConfig config;
        std::vector<LoadedEffectPart> parts;
        float currentTimer = 0.0f;
        int loopCount = 0;
        bool isWaitingDelay = true;
        bool isWaitingInterval = false;
        bool isFinished = false;
        float frameTimer = 0.0f;
        int currentFrame = 0;

        float mapX = -1.0f;
        float mapY = -1.0f;
        float screenOffsetX = 0.0f;
        float screenOffsetY = 0.0f;

        bool isDamageNumber = false;
        float currentLife = 0.0f;
        float maxLife = 2.0f;
        float baseOffsetY = 0.0f;
    };
    std::vector<ActiveEffect> m_activeEffects;

    Resource::EffectConfig m_wingConfig;
    std::vector<LoadedEffectPart> m_wingParts;
    bool m_hasWing = false;
    float m_wingTimer = 0.0f;
    int m_wingFrame = 0;

    int m_rightHandPhy = -1;
    int m_leftHandPhy = -1;
    int m_backPhy = -1;

    struct LoadedArmorPart {
        Resource::C3Model idleModel;
        Resource::C3Model walkModel;
        Resource::C3Model jumpModel;
        Resource::C3Model alertModel;
        Resource::C3Model attackModel[3];
        int textureId = -1;
        int asb = 5;
        int adb = 6;
    };
    std::vector<LoadedArmorPart> m_currentArmorParts;

    std::unordered_map<int16_t, int> m_puzzleTextures;
    Resource::DMapData m_currentDMap;
    Resource::PulData m_currentPul;
    int m_tileSize = 256;
    float m_cameraX = 0.0f, m_cameraY = 0.0f;
    float m_zoom = 1.0f;
    int m_mouseX = 0, m_mouseY = 0;

    int m_guiMainBarTexId = -1;
    int m_hpBarFullTexId = -1, m_hpBarEmptyTexId = -1;
    int m_texHpRed = -1, m_texHpBlack = -1, m_texHpOrange = -1;

    int m_frameCount = 0, m_currentFps = 0;
    float m_fpsTimer = 0.0f;
    int m_debugTexId = -1;
    int m_debugTexW = 0, m_debugTexH = 0;
    std::wstring m_lastDebugStr = L"";

    bool Initialize(HINSTANCE hInstance) {
        if (!m_window.Create(hInstance, L"Conquer Kayank - Engine Master")) return false;
        m_renderer.Initialize(m_window.m_hWnd, m_window.m_width, m_window.m_height);

        m_window.onMouseWheel = [this](int delta) {
            if (delta > 0) m_zoom += 0.1f;
            else if (delta < 0) m_zoom -= 0.1f;
            if (m_zoom < 0.5f) m_zoom = 0.5f;
            if (m_zoom > 3.0f) m_zoom = 3.0f;
            };

        std::string clientPath = "D:\\projetos\\kayank\\5017\\cliente";
        if (!m_resource.Initialize(clientPath)) return false;

        m_effectConfigs = m_resource.Parse3DEffects("ini\\3DEffect.ini");

        m_c3Paths = m_resource.ParseResIni("ini\\3dobj.ini");
        auto fxC3 = m_resource.ParseResIni("ini\\3DEffectObj.ini");
        m_c3Paths.insert(fxC3.begin(), fxC3.end());

        m_ddsPaths = m_resource.ParseResIni("ini\\3dtexture.ini");
        m_motionPaths = m_resource.ParseResIni("ini\\3Dmotion.ini");
        m_armorConfigs = m_resource.ParseArmorIni("ini\\armor.ini");

        auto loadGui = [&](const std::vector<std::string>& paths) -> int {
            for (const auto& p : paths) {
                auto data = m_resource.GetFileData(p);
                if (!data.empty()) return m_renderer.LoadTextureFromMemory(data.data(), data.size());
            }
            return -1;
            };

        m_guiMainBarTexId = loadGui({ "data/gui/main/main.dds", "data/gui/main/main1.dds", "data/gui/main.dds" });
        m_hpBarEmptyTexId = loadGui({ "data\\main\\ProgressHPA.dds" });
        m_hpBarFullTexId = loadGui({ "data\\main\\ProgressHP.dds" });
        m_texHpRed = Game::GenerateColorDDS(m_renderer, 220, 20, 20);
        m_texHpBlack = Game::GenerateColorDDS(m_renderer, 15, 15, 15);
        m_texHpOrange = Game::GenerateColorDDS(m_renderer, 255, 140, 0);

        auto [pTex, pW, pH] = Game::GenerateTextTexture(m_renderer, L"KayanK", RGB(255, 255, 255));
        m_player.nameTexId = pTex; m_player.nameW = pW; m_player.nameH = pH;

        std::string cursorPath = clientPath + "\\data\\Cursor\\Normal.ani";
        HCURSOR hCursor = LoadCursorFromFileA(cursorPath.c_str());
        if (hCursor) {
            SetClassLongPtr(m_window.m_hWnd, GCLP_HCURSOR, (LONG_PTR)hCursor);
            SetCursor(hCursor); ShowCursor(TRUE);
        }

        m_player.armorId = 300000;
        ChangeWeapon(0, 0);
        ChangeArmor(Game::ModelType::SmallFemale, m_player.armorId);

        

        auto monsterTexData = m_resource.GetFileData("c3\\texture\\104000000.dds");
        if (!monsterTexData.empty()) {
            m_monsterTextureId = m_renderer.LoadTextureFromMemory(monsterTexData.data(), monsterTexData.size());
        }
        Resource::C3Model monsterBase = m_resource.LoadC3Model("c3\\monster\\104N\\104000000.c3");
        Resource::C3Model monsterIdleAnim = m_resource.LoadC3Model("c3\\monster\\104N\\100.c3");
        Resource::C3Model monsterWalkAnim = m_resource.LoadC3Model("c3\\monster\\104N\\120.c3");
        Resource::C3Model monsterDieAnim = m_resource.LoadC3Model("c3\\monster\\104N\\330.c3");
        Resource::C3Model monsterDeadAnim = m_resource.LoadC3Model("c3\\monster\\104N\\331.c3");

        m_monsterIdleModel = monsterBase; m_monsterIdleModel.motions.clear();
        for (size_t i = 0; i < m_monsterIdleModel.phys.size(); i++) {
            if (monsterIdleAnim.isValid && !monsterIdleAnim.motions.empty()) m_monsterIdleModel.motions.push_back(monsterIdleAnim.motions[i % monsterIdleAnim.motions.size()]);
            else if (monsterWalkAnim.isValid && !monsterWalkAnim.motions.empty()) m_monsterIdleModel.motions.push_back(monsterWalkAnim.motions[i % monsterWalkAnim.motions.size()]);
        }

        m_monsterWalkModel = monsterBase; m_monsterWalkModel.motions.clear();
        for (size_t i = 0; i < m_monsterWalkModel.phys.size(); i++) {
            if (monsterWalkAnim.isValid && !monsterWalkAnim.motions.empty()) m_monsterWalkModel.motions.push_back(monsterWalkAnim.motions[i % monsterWalkAnim.motions.size()]);
            else if (monsterIdleAnim.isValid && !monsterIdleAnim.motions.empty()) m_monsterWalkModel.motions.push_back(monsterIdleAnim.motions[i % monsterIdleAnim.motions.size()]);
        }

        m_monsterDieModel = monsterBase; m_monsterDieModel.motions.clear();
        for (size_t i = 0; i < m_monsterDieModel.phys.size(); i++) {
            if (monsterDieAnim.isValid && !monsterDieAnim.motions.empty()) m_monsterDieModel.motions.push_back(monsterDieAnim.motions[i % monsterDieAnim.motions.size()]);
            else if (monsterIdleAnim.isValid && !monsterIdleAnim.motions.empty()) m_monsterDieModel.motions.push_back(monsterIdleAnim.motions[0]);
        }

        m_monsterDeadModel = monsterBase; m_monsterDeadModel.motions.clear();
        for (size_t i = 0; i < m_monsterDeadModel.phys.size(); i++) {
            if (monsterDeadAnim.isValid && !monsterDeadAnim.motions.empty()) m_monsterDeadModel.motions.push_back(monsterDeadAnim.motions[i % monsterDeadAnim.motions.size()]);
            else if (monsterIdleAnim.isValid && !monsterIdleAnim.motions.empty()) m_monsterDeadModel.motions.push_back(monsterIdleAnim.motions[0]);
        }

        auto gameMaps = m_resource.LoadGameMapDat("ini\\GameMap.dat");
        if (gameMaps.count(1005)) {
            m_currentDMap = m_resource.LoadDMap(gameMaps[1005].dmapPath);
            if (m_currentDMap.isValid) {
                m_player.mapX = m_currentDMap.width / 2.0f;
                m_player.mapY = m_currentDMap.height / 2.0f;

                Game::MonsterEntity faisao;
                faisao.mapX = m_player.mapX + 3.0f;
                faisao.mapY = m_player.mapY + 3.0f;
                faisao.startX = faisao.mapX; faisao.startY = faisao.mapY;

                faisao.hp = 1000;
                faisao.maxHp = 1000;
                faisao.visualHp = 1000.0f;

                auto [mTex, mW, mH] = Game::GenerateTextTexture(m_renderer, L"Pheasant", RGB(255, 255, 255));
                faisao.nameTexId = mTex; faisao.nameW = mW; faisao.nameH = mH;

                m_monsters.push_back(faisao);

                m_currentPul = m_resource.LoadPul(m_currentDMap.puzzleFile);
                m_tileSize = gameMaps[1005].tileSize > 0 ? gameMaps[1005].tileSize : 256;

                if (m_currentPul.isValid) {
                    for (int16_t tileId : m_currentPul.tiles) {
                        if (tileId != -1 && m_puzzleTextures.find(tileId) == m_puzzleTextures.end()) {
                            std::string section = "Puzzle" + std::to_string(tileId);
                            std::string ddsPath = m_resource.ParseAniSection(m_currentPul.aniFile, section);
                            if (!ddsPath.empty()) {
                                auto groundData = m_resource.GetFileData(ddsPath);
                                if (!groundData.empty()) m_puzzleTextures[tileId] = m_renderer.LoadTextureFromMemory(groundData.data(), groundData.size());
                            }
                        }
                    }
                }

                for (const auto& terrain : m_currentDMap.terrainObjects) {
                    std::string ddsPath = m_resource.ParseAniSection(terrain.aniPath, terrain.aniName);
                    if (!ddsPath.empty()) {
                        auto terrainTexData = m_resource.GetFileData(ddsPath);
                        if (!terrainTexData.empty()) {
                            Game::SceneObject obj;
                            obj.textureId = m_renderer.LoadTextureFromMemory(terrainTexData.data(), terrainTexData.size());
                            obj.mapX = (float)terrain.mapX; obj.mapY = (float)terrain.mapY;
                            obj.width = terrain.width; obj.height = terrain.height;
                            obj.offsetX = terrain.offsetX; obj.offsetY = terrain.offsetY;
                            m_sceneObjects.push_back(obj);
                        }
                    }
                }
            }
        }
        return true;
    }

    void ApplyAnim(Resource::C3Model& target, const Resource::C3Model& anim) {
        target.motions.clear();
        if (anim.isValid && !anim.motions.empty()) {
            for (size_t i = 0; i < target.phys.size(); i++) {
                target.motions.push_back(anim.motions[i % anim.motions.size()]);
            }
        }
    }

    void ApplyWalkAnim(Resource::C3Model& target, const Resource::C3Model& walk1, const Resource::C3Model& walk2) {
        target.motions.clear();
        if (walk1.isValid && walk2.isValid && !walk1.motions.empty() && !walk2.motions.empty()) {
            for (size_t i = 0; i < target.phys.size(); i++) {
                Resource::C3Motion walkMotion;
                auto m1 = walk1.motions[i % walk1.motions.size()];
                auto m2 = walk2.motions[i % walk2.motions.size()];
                walkMotion.boneCount = m1.boneCount; walkMotion.frameCount = m1.frameCount + m2.frameCount;
                for (auto kf : m1.keyframes) walkMotion.keyframes.push_back(kf);
                for (auto kf : m2.keyframes) { kf.pos += m1.frameCount; walkMotion.keyframes.push_back(kf); }
                target.motions.push_back(walkMotion);
            }
        }
        else {
            ApplyAnim(target, walk1);
        }
    }

    uint32_t GetWeaponPrefix(uint32_t rightId, uint32_t leftId) {
        if (rightId == 0 && leftId == 0) return 0;
        if (rightId == 0) return 0;

        uint32_t rightType = rightId / 100000;
        uint32_t leftType = (leftId > 0) ? (leftId / 100000) : 0;

        if (rightType == 5) { return rightId / 1000; }

        if (rightType == 4) {
            if (leftId == 0) { return rightId / 1000; }
            else if (leftType == 9) {
                uint32_t rightDoisDigitos = rightId / 10000;
                return 700 + rightDoisDigitos;
            }
            else if (leftType == 4) {
                uint32_t rightMeio = (rightId / 10000) % 10;
                uint32_t leftMeio = (leftId / 10000) % 10;
                return 600 + (rightMeio * 10) + leftMeio;
            }
        }

        return rightId / 1000;
    }

    Resource::C3Model GetActionModel(Game::ModelType type, uint32_t rightWeaponId, uint32_t leftWeaponId, Game::RoleActionType action) {
        uint32_t weaponPrefix = GetWeaponPrefix(rightWeaponId, leftWeaponId);
        uint32_t actionId = static_cast<uint32_t>(action);

        uint32_t motionId = (static_cast<uint32_t>(type) * 1000000) + (weaponPrefix * 1000) + actionId;
        if (m_motionPaths.find(motionId) != m_motionPaths.end()) {
            Resource::C3Model m = m_resource.LoadC3Model(m_motionPaths[motionId]);
            if (m.isValid && !m.motions.empty()) return m;
        }

        uint32_t fallbackMotion1 = (1 * 1000000) + (weaponPrefix * 1000) + actionId;
        if (m_motionPaths.find(fallbackMotion1) != m_motionPaths.end()) {
            Resource::C3Model m = m_resource.LoadC3Model(m_motionPaths[fallbackMotion1]);
            if (m.isValid && !m.motions.empty()) return m;
        }

        uint32_t fallbackMotion2 = (static_cast<uint32_t>(type) * 1000000) + actionId;
        if (m_motionPaths.find(fallbackMotion2) != m_motionPaths.end()) {
            Resource::C3Model m = m_resource.LoadC3Model(m_motionPaths[fallbackMotion2]);
            if (m.isValid && !m.motions.empty()) return m;
        }

        uint32_t fallbackMotion3 = (1 * 1000000) + actionId;
        if (m_motionPaths.find(fallbackMotion3) != m_motionPaths.end()) {
            Resource::C3Model m = m_resource.LoadC3Model(m_motionPaths[fallbackMotion3]);
            if (m.isValid && !m.motions.empty()) return m;
        }

        return Resource::C3Model();
    }

    void ChangeWing(const std::string& effectName) {
        if (effectName.empty() || effectName == "Nenhum") {
            m_hasWing = false;
            return;
        }

        auto it = m_effectConfigs.find(effectName);
        if (it == m_effectConfigs.end()) {
            std::string lowerName = effectName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            it = m_effectConfigs.find(lowerName);
        }

        if (it != m_effectConfigs.end()) {
            m_wingConfig = it->second;

            for (auto& p : m_wingParts) {
                if (p.textureId != -1) m_renderer.DeleteTexture(p.textureId);
            }
            m_wingParts.clear();

            for (auto& pCfg : m_wingConfig.parts) {
                LoadedEffectPart newPart;
                if (m_c3Paths.count(pCfg.effectId)) {
                    newPart.model = m_resource.LoadC3Model(m_c3Paths[pCfg.effectId]);
                }
                if (m_ddsPaths.count(pCfg.textureId)) {
                    auto texData = m_resource.GetFileData(m_ddsPaths[pCfg.textureId]);
                    if (!texData.empty()) {
                        newPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                    }
                }
                m_wingParts.push_back(newPart);
            }

            m_hasWing = true;
            m_wingFrame = 0;
            m_wingTimer = 0.0f;
        }
        else {
            m_hasWing = false;
        }
    }

    void ChangeWeapon(uint32_t rightId, uint32_t leftId) {
        m_player.rightHandWeaponId = rightId;
        m_player.leftHandWeaponId = leftId;

        auto loadWeaponPart = [&](uint32_t wid, Resource::C3Model& outModel, int& outTexId) {
            outTexId = -1; outModel = Resource::C3Model();
            if (wid == 0) return;

            if (m_c3Paths.find(wid) != m_c3Paths.end()) {
                outModel = m_resource.LoadC3Model(m_c3Paths[wid]);
            }

            uint32_t texIdsToTry[] = { wid, wid + 9, wid + 8, wid + 5, wid + 1, wid + 2 };
            for (uint32_t tid : texIdsToTry) {
                if (m_ddsPaths.find(tid) != m_ddsPaths.end()) {
                    auto texData = m_resource.GetFileData(m_ddsPaths[tid]);
                    if (!texData.empty()) {
                        outTexId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                        break;
                    }
                }
            }
            };

        if (m_weaponTextureId != -1) m_renderer.DeleteTexture(m_weaponTextureId);
        loadWeaponPart(rightId, m_weaponIdleModel, m_weaponTextureId);

        if (m_lweaponTextureId != -1) m_renderer.DeleteTexture(m_lweaponTextureId);
        loadWeaponPart(leftId, m_lweaponIdleModel, m_lweaponTextureId);

        ChangeArmor(m_player.modelType, m_player.armorId);
    }

    void ChangeArmor(Game::ModelType type, uint32_t armorId) {
        m_player.modelType = type;
        m_player.armorId = armorId;

        Resource::C3Model animIdle = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::StandBy);
        Resource::C3Model animWalkL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::RunL);
        Resource::C3Model animWalkR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::RunR);
        Resource::C3Model animJump = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Jump);

        Resource::C3Model animAlert = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Alert);
        if (!animAlert.isValid || animAlert.motions.empty()) animAlert = animIdle;

        Resource::C3Model animAttack1 = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_401);
        Resource::C3Model animAttack2 = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_402);
        Resource::C3Model animAttack3 = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_403);

        if (!animAttack2.isValid || animAttack2.motions.empty()) animAttack2 = animAttack1;
        if (!animAttack3.isValid || animAttack3.motions.empty()) animAttack3 = animAttack1;

        ApplyAnim(m_hairIdleModel, animIdle);
        ApplyAnim(m_hairJumpModel, animJump);
        ApplyAnim(m_hairAlertModel, animAlert);
        ApplyWalkAnim(m_hairWalkModel, animWalkL, animWalkR);

        ApplyAnim(m_hairAttackModel[0], animAttack1);
        ApplyAnim(m_hairAttackModel[1], animAttack2);
        ApplyAnim(m_hairAttackModel[2], animAttack3);

        uint32_t finalArmorId = (static_cast<uint32_t>(type) * 1000000) + armorId;
        if (m_armorConfigs.find(finalArmorId) == m_armorConfigs.end()) {
            finalArmorId = (static_cast<uint32_t>(type) * 1000000);
        }

        auto& armorCfg = m_armorConfigs[finalArmorId];

        for (auto& p : m_currentArmorParts) {
            if (p.textureId != -1) m_renderer.DeleteTexture(p.textureId);
        }
        m_currentArmorParts.clear();

        for (int i = 0; i < armorCfg.partCount; i++) {
            if (i >= armorCfg.parts.size()) break;
            auto& pCfg = armorCfg.parts[i];

            LoadedArmorPart newPart;
            newPart.asb = pCfg.asb;
            newPart.adb = pCfg.adb;

            if (m_c3Paths.find(pCfg.mesh) != m_c3Paths.end()) {
                std::string c3Path = m_c3Paths[pCfg.mesh];
                Resource::C3Model baseModel = m_resource.LoadC3Model(c3Path);

                if (baseModel.isValid) {
                    if (m_currentArmorParts.empty()) {
                        m_rightHandPhy = -1; m_leftHandPhy = -1; m_backPhy = -1;
                        for (size_t k = 0; k < baseModel.phys.size(); k++) {
                            std::string name = baseModel.phys[k].name;
                            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                            if (name == "point02" || name == "v_r_weapon" || name == "rweapon") m_rightHandPhy = (int)k;
                            else if (name == "point01" || name == "v_l_weapon" || name == "lweapon") m_leftHandPhy = (int)k;
                            else if (name == "point04" || name == "wing") m_backPhy = (int)k;
                        }
                    }

                    newPart.idleModel = baseModel; ApplyAnim(newPart.idleModel, animIdle);
                    newPart.walkModel = baseModel; ApplyWalkAnim(newPart.walkModel, animWalkL, animWalkR);
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);

                    newPart.attackModel[0] = baseModel; ApplyAnim(newPart.attackModel[0], animAttack1);
                    newPart.attackModel[1] = baseModel; ApplyAnim(newPart.attackModel[1], animAttack2);
                    newPart.attackModel[2] = baseModel; ApplyAnim(newPart.attackModel[2], animAttack3);
                }
            }

            if (m_ddsPaths.find(pCfg.texture) != m_ddsPaths.end()) {
                std::string ddsPath = m_ddsPaths[pCfg.texture];
                auto texData = m_resource.GetFileData(ddsPath);
                if (!texData.empty()) {
                    newPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                }
            }

            m_currentArmorParts.push_back(newPart);
        }
    }

    void Update(float deltaTime) {
        RECT rect;
        GetClientRect(m_window.m_hWnd, &rect);
        int currentWidth = rect.right - rect.left;
        int currentHeight = rect.bottom - rect.top;
        if (currentWidth > 0 && currentHeight > 0 && (currentWidth != m_window.m_width || currentHeight != m_window.m_height)) {
            m_window.m_width = currentWidth;
            m_window.m_height = currentHeight;
            m_renderer.Resize(currentWidth, currentHeight);
        }

        m_frameCount++; m_fpsTimer += deltaTime;
        if (m_fpsTimer >= 1.0f) { m_currentFps = m_frameCount; m_frameCount = 0; m_fpsTimer -= 1.0f; }

        HWND activeWindow = GetForegroundWindow();
        bool hasFocus = (activeWindow == m_window.m_hWnd);

        if (hasFocus) {
            static bool s_prev1 = false; bool curr1 = (GetAsyncKeyState('1') & 0x8000);
            if (curr1 && !s_prev1) ChangeArmor(Game::ModelType::SmallFemale, m_player.armorId);
            s_prev1 = curr1;

            static bool s_prev2 = false; bool curr2 = (GetAsyncKeyState('2') & 0x8000);
            if (curr2 && !s_prev2) ChangeArmor(Game::ModelType::BigFemale, m_player.armorId);
            s_prev2 = curr2;

            static bool s_prev3 = false; bool curr3 = (GetAsyncKeyState('3') & 0x8000);
            if (curr3 && !s_prev3) ChangeArmor(Game::ModelType::SmallMale, m_player.armorId);
            s_prev3 = curr3;

            static bool s_prev4 = false; bool curr4 = (GetAsyncKeyState('4') & 0x8000);
            if (curr4 && !s_prev4) ChangeArmor(Game::ModelType::BigMale, m_player.armorId);
            s_prev4 = curr4;

            static bool s_prev5 = false; bool curr5 = (GetAsyncKeyState('5') & 0x8000);
            if (curr5 && !s_prev5) ChangeWeapon(0, 0);
            s_prev5 = curr5;

            static bool s_prev6 = false; bool curr6 = (GetAsyncKeyState('6') & 0x8000);
            if (curr6 && !s_prev6) ChangeWeapon(410330, 0);
            s_prev6 = curr6;

            static bool s_prev7 = false; bool curr7 = (GetAsyncKeyState('7') & 0x8000);
            if (curr7 && !s_prev7) ChangeWeapon(410330, 410330);
            s_prev7 = curr7;

            static bool s_prev9 = false; bool curr9 = (GetAsyncKeyState('8') & 0x8000);
            if (curr9 && !s_prev9) ChangeWeapon(410330, 900090);
            s_prev9 = curr9;

            static bool s_prev0 = false; bool curr0 = (GetAsyncKeyState('9') & 0x8000);
            if (curr0 && !s_prev0) ChangeWeapon(500320, 0);
            s_prev0 = curr0;

            static bool s_prev8 = false; bool curr8 = (GetAsyncKeyState('0') & 0x8000);
            if (curr8 && !s_prev8) ChangeWing("_p_24_wing1_open110");
            s_prev8 = curr8;

            static bool s_prevE = false;
            bool currentE = (GetAsyncKeyState('E') & 0x8000) != 0;
            if (currentE && !s_prevE) {
                if (!m_effectList.empty()) {
                    m_currentEffectIndex++;
                    if (m_currentEffectIndex >= (int)m_effectList.size()) m_currentEffectIndex = 0;
                    LoadEffect(m_effectList[m_currentEffectIndex]);
                }
            }
            s_prevE = currentE;
        }

        POINT pt; GetCursorPos(&pt); ScreenToClient(m_window.m_hWnd, &pt);
        m_mouseX = pt.x; m_mouseY = pt.y;

        bool isMouseInside = (pt.x >= 0 && pt.x < m_window.m_width && pt.y >= 0 && pt.y < m_window.m_height);
        static bool s_prevLeftDown = false;
        bool currentLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool leftClicked = currentLeftDown && !s_prevLeftDown;
        s_prevLeftDown = currentLeftDown;

        if (m_player.isAlert && !m_player.isAttacking) {
            m_player.alertTimer -= deltaTime;
            if (m_player.alertTimer <= 0.0f) {
                m_player.isAlert = false;
                m_player.alertTimer = 0.0f;
            }
        }

        if (hasFocus && isMouseInside) {
            if (m_currentDMap.isValid && m_currentPul.isValid) {
                int puzzlePixelWidth = m_currentPul.horizontalTiles * m_tileSize;
                int puzzlePixelHeight = m_currentPul.verticalTiles * m_tileSize;
                Engine::IsometricCoordinateSystem coordSystem(puzzlePixelWidth, puzzlePixelHeight, m_currentDMap.height);

                float cx = m_window.m_width / 2.0f;
                float cy = m_window.m_height / 2.0f;
                float unzoomedMouseX = cx + (pt.x - cx) / m_zoom;
                float unzoomedMouseY = cy + (pt.y - cy) / m_zoom;

                int clickedMonsterIdx = -1;
                for (size_t i = 0; i < m_monsters.size(); i++) {
                    auto [mWorldX, mWorldY] = coordSystem.MapToScreen(m_monsters[i].mapX, m_monsters[i].mapY);
                    float drawX = mWorldX - m_cameraX;
                    float drawY = mWorldY - m_cameraY;
                    float zX = cx + (drawX - cx) * m_zoom;
                    float zY = cy + (drawY - cy) * m_zoom;

                    float mobW = 80.0f * m_zoom;
                    float mobH = 130.0f * m_zoom;
                    float mobLeft = zX - (mobW / 2.0f);
                    float mobRight = zX + (mobW / 2.0f);
                    float mobTop = zY - mobH;
                    float mobBottom = zY + (20.0f * m_zoom);

                    if (pt.x >= mobLeft && pt.x <= mobRight && pt.y >= mobTop && pt.y <= mobBottom) {
                        if (!m_monsters[i].isDead) {
                            clickedMonsterIdx = (int)i;
                            break;
                        }
                    }
                }

                auto [targetX, targetY] = coordSystem.ScreenToMap(unzoomedMouseX, unzoomedMouseY, m_cameraX, m_cameraY);

                if (leftClicked && clickedMonsterIdx != -1) {
                    m_player.targetMonsterIndex = clickedMonsterIdx;
                    m_player.isMoving = false;
                    m_player.hasQueuedAction = false;
                    m_player.isChasing = true;

                    m_player.isAlert = false;
                    m_player.alertTimer = 0.0f;
                }
                else if (currentLeftDown && clickedMonsterIdx == -1) {
                    m_player.targetMonsterIndex = -1;
                    m_player.isAttacking = false;
                    m_player.isChasing = false;

                    float dx = targetX - m_player.mapX;
                    float dy = targetY - m_player.mapY;
                    float dist = std::sqrt(dx * dx + dy * dy);

                    if (!m_player.isJumping) {
                        if (dist > 0.05f) m_player.facingAngle = -(std::atan2(dy, dx) - 0.78539f);

                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                            m_player.isJumping = true; m_player.isMoving = false;
                            m_player.jumpTimer = 0.0f; m_player.startMapX = m_player.mapX; m_player.startMapY = m_player.mapY;
                            m_player.targetMapX = targetX; m_player.targetMapY = targetY; m_player.currentFrame = 0;
                        }
                        else {
                            m_player.targetMapX = targetX; m_player.targetMapY = targetY; m_player.isMoving = true;
                        }
                    }
                    else if (leftClicked) {
                        m_player.hasQueuedAction = true;
                        m_player.queuedTargetX = targetX;
                        m_player.queuedTargetY = targetY;
                        m_player.queuedIsJump = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
                    }
                }
            }
        }

        if (m_player.isJumping) {
            float jumpDx = m_player.targetMapX - m_player.startMapX;
            float jumpDy = m_player.targetMapY - m_player.startMapY;
            float jumpDistance = std::sqrt(jumpDx * jumpDx + jumpDy * jumpDy);
            float jumpDuration = 0.5f + (jumpDistance * 0.05f);
            if (jumpDuration > 1.2f) jumpDuration = 1.2f;

            m_player.jumpTimer += deltaTime;
            float progress = m_player.jumpTimer / jumpDuration;

            if (progress >= 1.0f) {
                m_player.mapX = m_player.targetMapX; m_player.mapY = m_player.targetMapY;
                m_player.jumpZ = 0.0f; m_player.isJumping = false; m_player.currentFrame = 0;

                if (m_player.hasQueuedAction) {
                    m_player.hasQueuedAction = false;
                    float qdx = m_player.queuedTargetX - m_player.mapX;
                    float qdy = m_player.queuedTargetY - m_player.mapY;
                    float qdist = std::sqrt(qdx * qdx + qdy * qdy);
                    if (qdist > 0.05f) m_player.facingAngle = -(std::atan2(qdy, qdx) - 0.78539f);

                    if (m_player.queuedIsJump) {
                        m_player.isJumping = true; m_player.isMoving = false; m_player.jumpTimer = 0.0f;
                        m_player.startMapX = m_player.mapX; m_player.startMapY = m_player.mapY;
                        m_player.targetMapX = m_player.queuedTargetX; m_player.targetMapY = m_player.queuedTargetY;
                    }
                    else {
                        m_player.targetMapX = m_player.queuedTargetX; m_player.targetMapY = m_player.queuedTargetY;
                        m_player.isMoving = true;
                    }
                }
            }
            else {
                m_player.mapX = m_player.startMapX + (m_player.targetMapX - m_player.startMapX) * progress;
                m_player.mapY = m_player.startMapY + (m_player.targetMapY - m_player.startMapY) * progress;
                float maxJumpHeight = jumpDistance * 25.0f;
                if (maxJumpHeight > 300.0f) maxJumpHeight = 300.0f;
                if (maxJumpHeight < 20.0f) maxJumpHeight = 20.0f;
                m_player.jumpZ = 4.0f * maxJumpHeight * progress * (1.0f - progress);
            }
        }
        else if (m_player.targetMonsterIndex != -1 && m_player.targetMonsterIndex < m_monsters.size()) {
            auto& targetMob = m_monsters[m_player.targetMonsterIndex];

            float dx = targetMob.mapX - m_player.mapX;
            float dy = targetMob.mapY - m_player.mapY;
            float dist = std::sqrt(dx * dx + dy * dy);

            float attackRange = 1.5f;
            if (GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId) == 500) {
                attackRange = 8.0f;
            }

            if (dist > attackRange) {
                if (m_player.isAttacking) {
                    m_player.targetMonsterIndex = -1;
                    m_player.isChasing = false;
                }
                else if (m_player.isChasing) {
                    m_player.isMoving = true;
                    m_player.facingAngle = -(std::atan2(dy, dx) - 0.78539f);
                    float speed = 5.0f;
                    m_player.mapX += (dx / dist) * speed * deltaTime;
                    m_player.mapY += (dy / dist) * speed * deltaTime;
                }
            }
            else {
                m_player.isMoving = false;
                m_player.facingAngle = -(std::atan2(dy, dx) - 0.78539f);

                if (m_player.attackCooldown > 0.0f) {
                    m_player.attackCooldown -= deltaTime;
                    m_player.isAlert = true;
                    m_player.alertTimer = 5.0f;
                }
                else {
                    if (!m_player.isAttacking) {
                        m_player.isAttacking = true;
                        m_player.currentFrame = 0;
                        m_player.currentAttackIndex = rand() % 3;

                        m_player.isAlert = true;
                        m_player.alertTimer = 5.0f;
                    }
                }
            }
        }
        else if (m_player.isMoving) {
            float dx = m_player.targetMapX - m_player.mapX;
            float dy = m_player.targetMapY - m_player.mapY;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < 0.2f) {
                m_player.mapX = m_player.targetMapX; m_player.mapY = m_player.targetMapY;
                m_player.isMoving = false; m_player.currentFrame = 0;
            }
            else {
                float speed = 5.0f;
                m_player.mapX += (dx / dist) * speed * deltaTime;
                m_player.mapY += (dy / dist) * speed * deltaTime;
            }
        }

        m_player.animTimer += deltaTime;
        float currentAnimSpeed = 20.0f;
        if (m_player.isJumping) currentAnimSpeed = 15.0f;
        if (m_player.isAttacking) currentAnimSpeed = 25.0f;
        if (!m_player.isMoving && !m_player.isJumping && !m_player.isAttacking) currentAnimSpeed = 10.0f;

        if (m_player.animTimer >= (1.0f / currentAnimSpeed)) {
            m_player.currentFrame++;
            m_player.animTimer -= (1.0f / currentAnimSpeed);

            if (m_player.isAttacking) {
                if (m_player.currentFrame == 10) {
                    if (m_player.targetMonsterIndex != -1 && m_player.targetMonsterIndex < m_monsters.size()) {
                        auto& mob = m_monsters[m_player.targetMonsterIndex];
                        int damage = 405;
                        mob.hp -= damage;

                        std::string dmgStr = std::to_string(damage);
                        float digitSpacing = 30.0f;
                        float startX = -((dmgStr.length() - 1) * digitSpacing) / 2.0f;

                        for (size_t k = 0; k < dmgStr.length(); ++k) {
                            std::string effectName = "CountB" + std::string(1, dmgStr[k]);
                            LoadEffect(effectName, mob.mapX, mob.mapY, startX + (k * digitSpacing), 130.0f, true);
                        }

                        if (mob.hp <= 0 && !mob.isDead) {
                            mob.hp = 0;
                            mob.isDead = true;
                            mob.currentAction = 330;
                            mob.currentFrame = 0;
                            mob.animTimer = 0.0f;
                            mob.deathTimer = 0.0f;
                            mob.alpha = 1.0f;

                            m_player.targetMonsterIndex = -1;
                            m_player.isChasing = false;
                        }
                    }
                }

                if (m_player.currentFrame >= 20) {
                    m_player.currentFrame = 0;
                    m_player.isAttacking = false;
                    m_player.attackCooldown = 0.6f;

                    m_player.isAlert = true;
                    m_player.alertTimer = 5.0f;
                }
            }
        }

        if (m_hasWing) {
            m_wingTimer += deltaTime * 1000.0f;
            float interval = m_wingConfig.frameInterval > 0 ? (float)m_wingConfig.frameInterval : 33.0f;
            if (m_wingTimer >= interval) {
                m_wingFrame++;
                m_wingTimer -= interval;

                int maxFrames = 30;
                if (!m_wingParts.empty() && m_wingParts[0].model.isValid) {
                    if (!m_wingParts[0].model.motions.empty()) {
                        maxFrames = m_wingParts[0].model.motions[0].frameCount;
                    }
                    else if (!m_wingParts[0].model.ptcls.empty()) {
                        maxFrames = m_wingParts[0].model.ptcls[0].frames.size();
                    }
                }
                if (m_wingFrame >= maxFrames) m_wingFrame = 0;
            }
        }

        std::vector<Game::MonsterEntity> spawnedMonsters;

        for (auto it = m_monsters.begin(); it != m_monsters.end(); ) {
            auto& monster = *it;

            if (monster.isDead) {
                monster.animTimer += deltaTime;

                if (monster.currentAction == 330) {
                    if (monster.animTimer >= (1.0f / 12.0f)) {
                        monster.currentFrame++;
                        monster.animTimer -= (1.0f / 12.0f);

                        int maxFrames = 10;
                        if (m_monsterDieModel.isValid && !m_monsterDieModel.motions.empty()) {
                            maxFrames = m_monsterDieModel.motions[0].frameCount;
                        }

                        if (monster.currentFrame >= maxFrames - 1) {
                            monster.currentAction = 331;
                            monster.currentFrame = 0;
                            monster.animTimer = 0.0f;
                        }
                    }
                }
                else if (monster.currentAction == 331) {
                    monster.deathTimer += deltaTime;

                    monster.alpha = 1.0f - (monster.deathTimer / 2.0f);

                    if (monster.alpha <= 0.0f) {
                        monster.alpha = 0.0f;

                        Game::MonsterEntity faisao;
                        faisao.mapX = m_player.mapX + (rand() % 12 - 6);
                        faisao.mapY = m_player.mapY + (rand() % 12 - 6);
                        faisao.startX = faisao.mapX; faisao.startY = faisao.mapY;
                        faisao.hp = 1000;
                        faisao.maxHp = 1000;
                        faisao.visualHp = 1000.0f;
                        auto [mTex, mW, mH] = Game::GenerateTextTexture(m_renderer, L"Pheasant", RGB(255, 255, 255));
                        faisao.nameTexId = mTex; faisao.nameW = mW; faisao.nameH = mH;

                        spawnedMonsters.push_back(faisao);
                        it = m_monsters.erase(it);
                        continue;
                    }

                    if (monster.animTimer >= (1.0f / 10.0f)) {
                        monster.currentFrame++;
                        monster.animTimer -= (1.0f / 10.0f);
                        int maxFrames = 1;
                        if (m_monsterDeadModel.isValid && !m_monsterDeadModel.motions.empty()) {
                            maxFrames = m_monsterDeadModel.motions[0].frameCount;
                        }
                        if (monster.currentFrame >= maxFrames) monster.currentFrame = maxFrames - 1;
                    }
                }
                ++it;
                continue;
            }

            if (monster.visualHp > monster.hp) {
                float dropSpeed = monster.maxHp * 1.5f;
                monster.visualHp -= dropSpeed * deltaTime;
                if (monster.visualHp < monster.hp) monster.visualHp = monster.hp;
            }
            else if (monster.visualHp < monster.hp) {
                monster.visualHp = monster.hp;
            }

            if (monster.isMoving) {
                float dx = monster.targetX - monster.mapX;
                float dy = monster.targetY - monster.mapY;
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist < 0.1f) {
                    monster.mapX = monster.targetX; monster.mapY = monster.targetY;
                    monster.isMoving = false; monster.currentFrame = 0; monster.waitTimer = 0.0f;
                    monster.timeToWait = 1.0f + (rand() % 4);
                }
                else {
                    monster.facingAngle = -(std::atan2(dy, dx) - 0.78539f);
                    float speed = 2.0f;
                    monster.mapX += (dx / dist) * speed * deltaTime;
                    monster.mapY += (dy / dist) * speed * deltaTime;
                }

                monster.animTimer += deltaTime;
                if (monster.animTimer >= (1.0f / 15.0f)) { monster.currentFrame++; monster.animTimer -= (1.0f / 15.0f); }
            }
            else {
                monster.waitTimer += deltaTime;
                if (monster.waitTimer >= monster.timeToWait) {
                    float offsetX = (float)((rand() % 9) - 4); float offsetY = (float)((rand() % 9) - 4);
                    monster.targetX = monster.startX + offsetX; monster.targetY = monster.startY + offsetY;
                    monster.isMoving = true; monster.currentFrame = 0;
                }
                monster.animTimer += deltaTime;
                if (monster.animTimer >= (1.0f / 8.0f)) { monster.currentFrame++; monster.animTimer -= (1.0f / 8.0f); }
            }
            ++it;
        }

        for (const auto& newMob : spawnedMonsters) {
            m_monsters.push_back(newMob);
        }

        for (auto it = m_activeEffects.begin(); it != m_activeEffects.end(); ) {
            auto& effect = *it;
            float dtMs = deltaTime * 1000.0f;

            if (effect.isDamageNumber) {
                effect.currentLife += deltaTime;
                if (effect.currentLife >= effect.maxLife) {
                    effect.isFinished = true;
                }
                else {
                    float floatUp = effect.currentLife * 50.0f;
                    effect.screenOffsetY = effect.baseOffsetY + floatUp;
                }
            }

            if (effect.isWaitingDelay) {
                effect.currentTimer += dtMs;
                if (effect.currentTimer >= effect.config.delay) {
                    effect.isWaitingDelay = false;
                    effect.currentTimer = 0.0f;
                }
            }
            else if (effect.isWaitingInterval) {
                effect.currentTimer += dtMs;
                if (effect.currentTimer >= effect.config.loopInterval) {
                    effect.isWaitingInterval = false;
                    effect.currentTimer = 0.0f;
                }
            }
            else {
                effect.frameTimer += dtMs;
                float interval = effect.config.frameInterval > 0 ? (float)effect.config.frameInterval : 33.0f;

                if (effect.frameTimer >= interval) {
                    effect.currentFrame++;
                    effect.frameTimer -= interval;

                    if (effect.currentFrame >= 30) {
                        effect.loopCount++;

                        if (effect.config.loopTime != 99999999 && effect.loopCount >= effect.config.loopTime) {
                            effect.isFinished = true;
                        }
                        else {
                            if (effect.config.loopInterval > 0) {
                                effect.isWaitingInterval = true;
                                effect.currentTimer = 0.0f;
                            }
                            effect.currentFrame = 0;
                        }
                    }
                }
            }

            if (effect.isFinished) {
                for (auto& part : effect.parts) {
                    if (part.textureId != -1) m_renderer.DeleteTexture(part.textureId);
                }
                it = m_activeEffects.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void DrawWorld() {
        if (!m_currentPul.isValid) return;

        int puzzlePixelWidth = m_currentPul.horizontalTiles * m_tileSize;
        int puzzlePixelHeight = m_currentPul.verticalTiles * m_tileSize;
        Engine::IsometricCoordinateSystem coordSystem(puzzlePixelWidth, puzzlePixelHeight, m_currentDMap.height);

        auto [worldX, worldY] = coordSystem.MapToScreen(m_player.mapX, m_player.mapY);
        m_cameraX = worldX - (m_window.m_width / 2.0f);
        m_cameraY = worldY - (m_window.m_height / 2.0f);

        float cx = m_window.m_width / 2.0f; float cy = m_window.m_height / 2.0f;
        float viewWidth = m_window.m_width / m_zoom; float viewHeight = m_window.m_height / m_zoom;
        float startWorldX = m_cameraX + cx - (cx / m_zoom); float startWorldY = m_cameraY + cy - (cy / m_zoom);

        int startTileX = (std::max)(0, (int)(startWorldX / m_tileSize) - 1);
        int startTileY = (std::max)(0, (int)(startWorldY / m_tileSize) - 1);
        int endTileX = (std::min)((int)m_currentPul.horizontalTiles, (int)((startWorldX + viewWidth) / m_tileSize) + 2);
        int endTileY = (std::min)((int)m_currentPul.verticalTiles, (int)((startWorldY + viewHeight) / m_tileSize) + 2);

        for (int x = startTileX; x < endTileX; x++) {
            for (int y = startTileY; y < endTileY; y++) {
                int16_t tileId = m_currentPul.tiles[y * m_currentPul.horizontalTiles + x];
                if (tileId != -1 && m_puzzleTextures.count(tileId)) {
                    float drawX = (x * m_tileSize) - m_cameraX; float drawY = (y * m_tileSize) - m_cameraY;
                    float zX = cx + (drawX - cx) * m_zoom; float zY = cy + (drawY - cy) * m_zoom;
                    float zW = m_tileSize * m_zoom;
                    m_renderer.DrawSprite(m_puzzleTextures[tileId], (int)zX, (int)zY, (int)std::ceil(zW) + 1, (int)std::ceil(zW) + 1);
                }
            }
        }

        struct RenderNode { float depth; int type; int index; };
        std::vector<RenderNode> renderQueue;
        renderQueue.push_back({ m_player.mapX + m_player.mapY, 0, 0 });
        for (size_t i = 0; i < m_monsters.size(); i++) renderQueue.push_back({ m_monsters[i].mapX + m_monsters[i].mapY, 1, (int)i });
        for (size_t i = 0; i < m_sceneObjects.size(); i++) renderQueue.push_back({ m_sceneObjects[i].mapX + m_sceneObjects[i].mapY, 2, (int)i });
        std::sort(renderQueue.begin(), renderQueue.end(), [](const RenderNode& a, const RenderNode& b) { return a.depth < b.depth; });

        for (const auto& node : renderQueue) {
            if (node.type == 0) {
                Resource::C3Model* mainBodyModel = nullptr;
                if (!m_currentArmorParts.empty()) {
                    if (m_player.isAttacking) mainBodyModel = &m_currentArmorParts[0].attackModel[m_player.currentAttackIndex];
                    else if (m_player.isJumping) mainBodyModel = &m_currentArmorParts[0].jumpModel;
                    else if (m_player.isMoving) mainBodyModel = &m_currentArmorParts[0].walkModel;
                    else if (m_player.isAlert) mainBodyModel = &m_currentArmorParts[0].alertModel;
                    else mainBodyModel = &m_currentArmorParts[0].idleModel;
                }

                for (const auto& part : m_currentArmorParts) {
                    Resource::C3Model* activeModel = (Resource::C3Model*)&part.idleModel;
                    if (m_player.isAttacking) activeModel = (Resource::C3Model*)&part.attackModel[m_player.currentAttackIndex];
                    else if (m_player.isJumping) activeModel = (Resource::C3Model*)&part.jumpModel;
                    else if (m_player.isMoving) activeModel = (Resource::C3Model*)&part.walkModel;
                    else if (m_player.isAlert) activeModel = (Resource::C3Model*)&part.alertModel;

                    if (activeModel->isValid)
                        m_renderer.DrawMesh3D(*activeModel, cx, cy - (m_player.jumpZ * m_zoom), part.textureId, m_player.currentFrame, m_player.facingAngle, 0.0f, true, m_zoom, nullptr, -1, "", part.asb, part.adb);
                }

                Resource::C3Model* activeHair = &m_hairIdleModel;
                Resource::C3Model* activeWeapon = &m_weaponIdleModel;
                Resource::C3Model* activeLWeapon = &m_lweaponIdleModel;

                if (m_player.isAttacking) { activeHair = &m_hairAttackModel[m_player.currentAttackIndex]; }
                else if (m_player.isJumping) { activeHair = &m_hairJumpModel; }
                else if (m_player.isMoving) { activeHair = &m_hairWalkModel; }
                else if (m_player.isAlert) { activeHair = &m_hairAlertModel; }

                if (activeHair->isValid) m_renderer.DrawMesh3D(*activeHair, cx, cy - (m_player.jumpZ * m_zoom), m_hairTextureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom);

                int targetRightBone = m_rightHandPhy;
                if (m_player.rightHandWeaponId / 100000 == 5) targetRightBone = m_leftHandPhy;

                if (activeWeapon->isValid && mainBodyModel) m_renderer.DrawMesh3D(*activeWeapon, cx, cy - (m_player.jumpZ * m_zoom), m_weaponTextureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, targetRightBone);

                if (activeLWeapon->isValid && mainBodyModel) m_renderer.DrawMesh3D(*activeLWeapon, cx, cy - (m_player.jumpZ * m_zoom), m_lweaponTextureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, m_leftHandPhy);

                if (m_hasWing && mainBodyModel) {
                    int attachBone = (m_backPhy != -1) ? m_backPhy : 0;

                    for (size_t i = 0; i < m_wingParts.size(); i++) {
                        auto& part = m_wingParts[i];
                        if (part.model.isValid) {
                            int asb = m_wingConfig.parts[i].asb;
                            int adb = m_wingConfig.parts[i].adb;

                            float wingOffsetX = m_wingConfig.offsetX * m_zoom;
                            float wingOffsetY = (m_wingConfig.offsetY - 20.0f) * m_zoom;

                            float wingRotation = m_player.facingAngle;
                            float wingPitch = 1.5708f;

                            if (!part.model.phys.empty()) {
                                m_renderer.DrawMesh3D(part.model, cx + wingOffsetX, cy - (m_player.jumpZ * m_zoom) - wingOffsetY, part.textureId, m_wingFrame, wingRotation, wingPitch, false, m_zoom, mainBodyModel, attachBone, "", asb, adb, 1.0f, true);
                            }
                            if (!part.model.ptcls.empty()) {
                                m_renderer.DrawParticles(part.model, cx + wingOffsetX, cy - (m_player.jumpZ * m_zoom) - wingOffsetY, part.textureId, m_wingFrame, wingRotation, wingPitch, m_zoom, asb, adb);
                            }
                        }
                    }
                }
            }
            else if (node.type == 1) {
                auto& monster = m_monsters[node.index];

                Resource::C3Model* activeMonsterModel = &m_monsterIdleModel;
                if (monster.isDead) {
                    if (monster.currentAction == 330) activeMonsterModel = &m_monsterDieModel;
                    else if (monster.currentAction == 331) activeMonsterModel = &m_monsterDeadModel;
                }
                else if (monster.isMoving) {
                    activeMonsterModel = &m_monsterWalkModel;
                }

                if (activeMonsterModel->isValid) {
                    auto [mWorldX, mWorldY] = coordSystem.MapToScreen(monster.mapX, monster.mapY);
                    float drawX = mWorldX - m_cameraX; float drawY = mWorldY - m_cameraY;
                    float zX = cx + (drawX - cx) * m_zoom; float zY = cy + (drawY - cy) * m_zoom;

                    m_renderer.DrawMesh3D(*activeMonsterModel, zX, zY, m_monsterTextureId, monster.currentFrame, monster.facingAngle, 0.0f, false, m_zoom, nullptr, -1, "", 5, 6, monster.alpha);

                    if (!monster.isDead && m_texHpBlack != -1 && m_texHpRed != -1 && m_texHpOrange != -1) {
                        int mobHpBarW = 40; int mobHpBarH = 4;
                        int hpX = (int)zX - (int)((mobHpBarW * m_zoom) / 2);
                        int hpY = (int)zY - (int)(110 * m_zoom);

                        m_renderer.DrawSprite(m_texHpBlack, hpX - 1, hpY - 1, (int)(mobHpBarW * m_zoom) + 2, (int)(mobHpBarH * m_zoom) + 2);

                        float visualRatio = monster.visualHp / (float)monster.maxHp;
                        if (visualRatio < 0.0f) visualRatio = 0.0f;
                        int visualW = (int)(mobHpBarW * visualRatio);
                        if (visualW > 0) m_renderer.DrawSprite(m_texHpOrange, hpX, hpY, (int)(visualW * m_zoom), (int)(mobHpBarH * m_zoom));

                        float hpRatio = (float)monster.hp / (float)monster.maxHp;
                        if (hpRatio < 0.0f) hpRatio = 0.0f;
                        int currentHpW = (int)(mobHpBarW * hpRatio);
                        if (currentHpW > 0) m_renderer.DrawSprite(m_texHpRed, hpX, hpY, (int)(currentHpW * m_zoom), (int)(mobHpBarH * m_zoom));

                        if (monster.nameTexId != -1) {
                            int nameX = (int)zX - (monster.nameW / 2);
                            int nameY = hpY - monster.nameH - 2;
                            m_renderer.DrawSprite(monster.nameTexId, nameX, nameY, monster.nameW, monster.nameH);
                        }
                    }
                }
            }
            else if (node.type == 2) {
                auto& obj = m_sceneObjects[node.index];
                auto [objWorldX, objWorldY] = coordSystem.MapToScreen(obj.mapX, obj.mapY);
                float drawX = objWorldX - m_cameraX - obj.offsetX; float drawY = objWorldY - m_cameraY - obj.offsetY;
                float zX = cx + (drawX - cx) * m_zoom; float zY = cy + (drawY - cy) * m_zoom;
                float zW = obj.width * m_zoom; float zH = obj.height * m_zoom;
                m_renderer.DrawSprite(obj.textureId, (int)zX, (int)zY, (int)zW, (int)zH);
            }
        }

        for (auto& effect : m_activeEffects) {
            if (effect.isWaitingDelay || effect.isWaitingInterval || effect.isFinished) continue;

            float drawCx = cx;
            float drawCy = cy - (m_player.jumpZ * m_zoom);

            if (effect.mapX != -1.0f && effect.mapY != -1.0f) {
                auto [eWorldX, eWorldY] = coordSystem.MapToScreen(effect.mapX, effect.mapY);
                float drawX = eWorldX - m_cameraX;
                float drawY = eWorldY - m_cameraY;
                drawCx = cx + (drawX - cx) * m_zoom;
                drawCy = cy + (drawY - cy) * m_zoom;
            }

            drawCx += effect.screenOffsetX * m_zoom;
            drawCy -= effect.screenOffsetY * m_zoom;

            float eScale = m_zoom;
            float eAlpha = 1.0f;

            if (effect.isDamageNumber) {
                float life = effect.currentLife;
                float baseSize = 0.70f;

                if (life < 0.1f) {
                    float progress = life / 0.1f;
                    eScale = m_zoom * (3.0f - (3.0f - baseSize) * progress);
                }
                else if (life < 0.15f) {
                    float progress = (life - 0.1f) / 0.05f;
                    eScale = m_zoom * (baseSize - 0.1f * std::sin(progress * 3.14159f));
                }
                else {
                    eScale = m_zoom * baseSize;
                }

                if (life >= 1.5f) {
                    float progress = (life - 1.5f) / 0.5f;
                    eAlpha = 1.0f - progress;
                    if (eAlpha < 0.0f) eAlpha = 0.0f;
                }
            }

            for (size_t i = 0; i < effect.parts.size(); i++) {
                auto& part = effect.parts[i];
                int asb = effect.config.parts[i].asb;
                int adb = effect.config.parts[i].adb;

                if (part.model.isValid) {
                    if (!part.model.phys.empty()) {
                        m_renderer.DrawMesh3D(part.model, drawCx, drawCy, part.textureId, effect.currentFrame, 0.0f, 0.0f, false, eScale, nullptr, -1, "", asb, adb, eAlpha);
                    }
                    if (!part.model.ptcls.empty()) {
                        for (auto& ptcl : part.model.ptcls) {
                            ptcl.globalAlpha = eAlpha;
                        }
                        m_renderer.DrawParticles(part.model, drawCx, drawCy, part.textureId, effect.currentFrame, 0.0f, 0.0f, eScale, asb, adb);
                    }
                }
            }
        }
    }

    void DrawGUI() {
        if (m_guiMainBarTexId != -1) {
            int barW = 800; int barH = 120;
            m_renderer.DrawSprite(m_guiMainBarTexId, (m_window.m_width - barW) / 2, m_window.m_height - barH, barW, barH);
        }
        int hpW = 120, hpH = 120, hpX = 20, hpY = m_window.m_height - hpH - 20;
        if (m_hpBarEmptyTexId != -1) m_renderer.DrawSprite(m_hpBarEmptyTexId, hpX, hpY, hpW, hpH);
        if (m_hpBarFullTexId != -1) m_renderer.DrawSprite(m_hpBarFullTexId, hpX, hpY, hpW, hpH);

        int hpBarW = 60, hpBarH = 6;
        int headX = (m_window.m_width - (int)(hpBarW * m_zoom)) / 2;
        int headY = (m_window.m_height / 2) - (m_player.jumpZ * m_zoom) - (130 * m_zoom);

        if (m_texHpBlack != -1 && m_texHpRed != -1) {
            m_renderer.DrawSprite(m_texHpBlack, headX - 1, headY - 1, (int)(hpBarW * m_zoom) + 2, (int)(hpBarH * m_zoom) + 2);
            m_renderer.DrawSprite(m_texHpRed, headX, headY, (int)((hpBarW * 0.8f) * m_zoom), (int)(hpBarH * m_zoom));
        }

        if (m_player.nameTexId != -1) {
            int nameX = (m_window.m_width - m_player.nameW) / 2;
            int nameY = headY - m_player.nameH - 5;
            m_renderer.DrawSprite(m_player.nameTexId, nameX, nameY, m_player.nameW, m_player.nameH);
        }

        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        wchar_t debugBuffer[256];
        swprintf(debugBuffer, 256, L"[KayanK-CO] Ping: 0, Fps: %d, %02d:%02d %02d/%02d/%04d",
            m_currentFps,
            localTime->tm_hour, localTime->tm_min, localTime->tm_mday, localTime->tm_mon + 1, localTime->tm_year + 1900);

        std::wstring currentDebugStr(debugBuffer);

        if (currentDebugStr != m_lastDebugStr) {
            if (m_debugTexId != -1) m_renderer.DeleteTexture(m_debugTexId);
            auto [tId, tW, tH] = Game::GenerateTextTexture(m_renderer, currentDebugStr, RGB(255, 255, 255));
            m_debugTexId = tId; m_debugTexW = tW; m_debugTexH = tH;
            m_lastDebugStr = currentDebugStr;
        }

        if (m_debugTexId != -1) m_renderer.DrawSprite(m_debugTexId, 10, 10, m_debugTexW, m_debugTexH);
    }

    void LoadEffect(const std::string& effectName, float mapX = -1.0f, float mapY = -1.0f, float screenOffsetX = 0.0f, float screenOffsetY = 0.0f, bool isDamage = false) {
        m_currentEffectName = effectName;

        auto it = m_effectConfigs.find(effectName);
        if (it == m_effectConfigs.end()) {
            std::string lowerName = effectName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            it = m_effectConfigs.find(lowerName);
            if (it == m_effectConfigs.end()) return;
        }

        auto& config = it->second;

        ActiveEffect newActiveEffect;
        newActiveEffect.config = config;
        newActiveEffect.mapX = mapX;
        newActiveEffect.mapY = mapY;
        newActiveEffect.screenOffsetX = screenOffsetX;
        newActiveEffect.screenOffsetY = screenOffsetY;

        newActiveEffect.baseOffsetY = screenOffsetY;
        newActiveEffect.isDamageNumber = isDamage;
        newActiveEffect.currentLife = 0.0f;
        newActiveEffect.maxLife = 2.0f;

        if (config.delay <= 0) newActiveEffect.isWaitingDelay = false;

        for (int i = 0; i < config.amount; i++) {
            if (i >= config.parts.size()) break;
            auto& partConfig = config.parts[i];
            LoadedEffectPart newPart;

            if (m_c3Paths.find(partConfig.effectId) != m_c3Paths.end()) {
                std::string c3Path = m_c3Paths[partConfig.effectId];
                newPart.model = m_resource.LoadC3Model(c3Path);
            }
            if (m_ddsPaths.find(partConfig.textureId) != m_ddsPaths.end()) {
                std::string ddsPath = m_ddsPaths[partConfig.textureId];
                auto texData = m_resource.GetFileData(ddsPath);
                if (!texData.empty()) {
                    newPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                }
            }
            newActiveEffect.parts.push_back(newPart);
        }
        m_activeEffects.push_back(newActiveEffect);
    }

    void Run() {
        srand((unsigned int)time(NULL));
        MSG msg = {};
        auto lastTime = std::chrono::high_resolution_clock::now();
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg); DispatchMessage(&msg);
            }
            else {
                auto currentTime = std::chrono::high_resolution_clock::now();
                float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
                lastTime = currentTime;
                if (deltaTime > 0.1f) deltaTime = 0.1f;
                Update(deltaTime);
                m_renderer.BeginFrame();
                DrawWorld(); DrawGUI();
                m_renderer.EndFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    AllocConsole(); FILE* dummy; freopen_s(&dummy, "CONOUT$", "w", stdout);
    Application app; if (app.Initialize(hInstance)) app.Run();
    return 0;
}