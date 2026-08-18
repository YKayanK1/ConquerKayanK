// ============================================================================
// Conquer Kayank Engine - Master Version
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
#include <sstream>

#include "../Graphics/Graphics.h"
#include "../Graphics/Graphics_D3D.h"
#include "../Resource/Resource.h"
#include "../Audio/Audio.h"

#include "Engine_Window.h"
#include "Engine_Math.h"
#include "Game_Entities.h"
#include "Game_Utils.h"

#include <DirectXMath.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// A Ponte Indestrutível do Linker (Recorte de Interface)
extern "C" void SetSpriteUV(float u1, float v1, float u2, float v2);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC OriginalWndProc = nullptr;

LRESULT CALLBACK HookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
}

class Application {
public:
    std::string m_clientPath = "D:\\projetos\\kayank\\5017\\cliente";
    std::string m_currentEffectName = "Nenhum";

    Engine::WindowManager m_window;
    Graphics::SceneRenderer m_renderer;
    Resource::Manager m_resource;
    Audio::Manager m_audio;

    Game::PlayerEntity m_player;
    uint32_t m_currentArmetId = 0;
    uint32_t m_currentGarmentId = 0;

    struct ExpandedMonsterEntity : public Game::MonsterEntity {
        uint32_t meshId;
    };
    std::vector<ExpandedMonsterEntity> m_monsters;
    std::vector<Game::SceneObject> m_sceneObjects;

    // [NOVO] Adicionado o modelo de Correr separado do Andar
    Resource::C3Model m_hairIdleModel, m_hairWalkModel, m_hairRunModel, m_hairJumpModel, m_hairAlertModel;
    std::unordered_map<int, Resource::C3Model> m_hairAttackModels;

    int m_hairTextureId = -1;

    std::vector<std::string> m_effectList;
    std::unordered_map<std::string, Resource::EffectConfig> m_effectConfigs;
    std::unordered_map<uint32_t, std::string> m_c3Paths;
    std::unordered_map<uint32_t, std::string> m_ddsPaths;
    std::unordered_map<uint32_t, std::string> m_motionPaths;

    std::unordered_map<uint32_t, Resource::ArmorConfig> m_armorConfigs;
    std::unordered_map<uint32_t, Resource::ArmorConfig> m_armetConfigs;
    std::unordered_map<uint32_t, Resource::WeaponConfig> m_weaponConfigs;
    std::unordered_map<uint32_t, std::string> m_action3DEffects;
    std::unordered_map<std::string, std::string> m_actionSounds;

    struct LoadedEffectPart {
        Resource::C3Model model;
        int textureId = -1;
    };

    struct ActiveEffect {
        std::string name;
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

        bool isFirstFrame = true;
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
    int m_headPhy = -1;

    float m_weaponEffectTimer = 0.0f;
    int m_weaponEffectFrame = 0;

    struct LoadedArmorPart {
        Resource::C3Model idleModel;
        Resource::C3Model walkModel;
        Resource::C3Model runModel; // [NOVO] Guarda a animação de correr!
        Resource::C3Model jumpModel;
        Resource::C3Model alertModel;
        std::unordered_map<int, Resource::C3Model> attackModels;
        int textureId = -1;
        int asb = 5;
        int adb = 6;
    };
    std::vector<LoadedArmorPart> m_currentArmorParts;
    std::vector<LoadedArmorPart> m_currentGarmentParts;
    std::vector<LoadedArmorPart> m_currentArmetParts;

    struct LoadedWeaponPart {
        Resource::C3Model model;
        int textureId = -1;
        int asb = 5;
        int adb = 6;

        bool hasEffect = false;
        Resource::EffectConfig effectConfig;
        std::vector<LoadedEffectPart> effectParts;
        std::vector<Graphics::ShapeRenderState> shapeStates;
    };
    std::vector<LoadedWeaponPart> m_rightWeaponParts;
    std::vector<LoadedWeaponPart> m_leftWeaponParts;

    struct GameItemDef {
        uint32_t id;
        std::string name;
    };
    std::vector<GameItemDef> m_gameItems;
    std::vector<GameItemDef> m_armorsList;
    std::vector<GameItemDef> m_garmentList;
    std::vector<GameItemDef> m_armetList;
    std::vector<GameItemDef> m_rightWeaponList;
    std::vector<GameItemDef> m_leftWeaponList;

    struct MonsterDef {
        uint32_t id;
        std::string name;
        uint32_t meshId;
        int maxLife;
    };
    std::vector<MonsterDef> m_monsterDB;

    struct MagicDef {
        uint32_t id;
        uint32_t level;
        std::string name;
        std::string intoneEffect;
        std::string senderEffect;
        std::string targetEffect;
        std::string soundPath;
        std::string tmeFile;
        uint32_t actionId;
    };
    std::vector<MagicDef> m_magicDB;

    struct CachedMonster {
        Resource::C3Model model;
        int textureId = -1;
        int asb = 5;
        int adb = 6;
    };
    std::unordered_map<uint64_t, CachedMonster> m_monsterCache;

    std::unordered_map<int16_t, int> m_puzzleTextures;
    Resource::DMapData m_currentDMap;
    Resource::PulData m_currentPul;
    int m_tileSize = 256;
    float m_cameraX = 0.0f, m_cameraY = 0.0f;
    float m_zoom = 1.0f;
    int m_mouseX = 0, m_mouseY = 0;

    int m_texMainDialog1 = -1;
    int m_texMainDialog2 = -1;

    // [NOVO] IDs do Checkbox de Correr/Andar
    int m_texRunChk1 = -1; // Vermelha (Andar)
    int m_texRunChk2 = -1; // Amarela (Correr)
    bool m_isRunning = false; // Começa andando

    int m_texHpRed = -1;
    int m_texHpBlack = -1;
    int m_texHpOrange = -1;

    int m_frameCount = 0, m_currentFps = 0;
    float m_fpsTimer = 0.0f;
    int m_debugTexId = -1;
    int m_debugTexW = 0, m_debugTexH = 0;
    std::wstring m_lastDebugStr = L"";

    void PlayActionSound(uint32_t meshId, uint32_t weaponPrefix, uint32_t actionId) {
        if (weaponPrefix == 0) {
            weaponPrefix = 999;
        }

        std::string key = std::to_string(meshId) + "." + std::to_string(weaponPrefix) + "." + std::to_string(actionId);
        auto it = m_actionSounds.find(key);

        if (it == m_actionSounds.end() && weaponPrefix != 999) {
            std::string fallbackKey = std::to_string(meshId) + ".999." + std::to_string(actionId);
            it = m_actionSounds.find(fallbackKey);
        }

        if (it != m_actionSounds.end()) {
            std::string snd = it->second;
            if (!snd.empty() && snd != "none" && snd != "NULL" && snd != "0") {
                m_audio.PlaySoundEffect(m_clientPath + "\\" + snd);
            }
        }
    }

    void LoadMagicDB() {
        auto data = m_resource.GetFileData("ini\\MagicType.txt");
        if (data.empty()) return;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;

        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::vector<std::string> tokens;
            std::string token;
            while (ls >> token) {
                tokens.push_back(token);
            }

            size_t n = tokens.size();
            if (n > 15) {
                MagicDef def;
                try { def.id = std::stoul(tokens[0]); }
                catch (...) { continue; }
                try { def.level = std::stoul(tokens[7]); }
                catch (...) { def.level = 0; }

                def.name = tokens[2];
                std::replace(def.name.begin(), def.name.end(), '~', ' ');

                try { def.actionId = std::stoul(tokens[n - 15]); }
                catch (...) { def.actionId = 401; }

                def.intoneEffect = tokens[n - 13];
                def.senderEffect = tokens[n - 12];
                def.targetEffect = tokens[n - 7];
                def.soundPath = tokens[n - 6];
                def.tmeFile = tokens[n - 5];

                if (def.intoneEffect == "NULL") def.intoneEffect = "";
                if (def.senderEffect == "NULL") def.senderEffect = "";
                if (def.targetEffect == "NULL") def.targetEffect = "";
                if (def.soundPath == "NULL") def.soundPath = "";
                if (def.tmeFile == "NULL") def.tmeFile = "";

                if (def.id > 0) {
                    m_magicDB.push_back(def);
                }
            }
        }
    }

    void LoadMonsterDB() {
        auto data = m_resource.GetFileData("ini\\dbmonster.txt");
        if (data.empty()) return;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;

        std::getline(iss, line);

        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            std::replace(line.begin(), line.end(), ';', ' ');
            std::istringstream ls(line);
            MonsterDef def;
            if (ls >> def.id >> def.name >> def.meshId >> def.maxLife) {
                m_monsterDB.push_back(def);
            }
        }
    }

    void LoadItemTypes(const std::string& path) {
        auto data = m_resource.GetFileData(path);
        if (data.empty()) return;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;

        GameItemDef nullItem = { 0, "Nenhum" };
        m_armorsList.push_back(nullItem);
        m_garmentList.push_back(nullItem);
        m_armetList.push_back(nullItem);
        m_rightWeaponList.push_back(nullItem);
        m_leftWeaponList.push_back(nullItem);

        while (std::getline(iss, line)) {
            line.erase(0, line.find_first_not_of(" \r\n\t"));
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (line.empty() || line.find("Amount=") == 0 || line[0] == ';' || line[0] == '#') continue;

            std::istringstream ls(line);
            uint32_t id;
            std::string name;

            if (ls >> id >> name) {
                GameItemDef item = { id, name };
                m_gameItems.push_back(item);

                if (id >= 130000 && id < 140000) {
                    m_armorsList.push_back(item);
                }
                else if (id >= 180000 && id <= 199999) {
                    m_garmentList.push_back(item);
                }
                else if ((id >= 110000 && id < 120000) || (id >= 140000 && id < 150000)) {
                    m_armetList.push_back(item);
                }
                else if (id >= 400000 && id < 600000) {
                    m_rightWeaponList.push_back(item);
                    m_leftWeaponList.push_back(item);
                }
                else if (id >= 900000 && id < 1000000) {
                    m_leftWeaponList.push_back(item);
                }
                else if (id >= 1050000 && id < 1060000) {
                    m_leftWeaponList.push_back(item);
                }
            }
        }
    }

    bool Initialize(HINSTANCE hInstance) {
        if (!m_window.Create(hInstance, L"Conquer Kayank - Engine Master")) return false;
        m_renderer.Initialize(m_window.m_hWnd, m_window.m_width, m_window.m_height);

        m_audio.Initialize();

        m_window.onMouseWheel = [this](int delta) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse) return;

            if (delta > 0) m_zoom += 0.1f;
            else if (delta < 0) m_zoom -= 0.1f;
            if (m_zoom < 0.5f) m_zoom = 0.5f;
            if (m_zoom > 3.0f) m_zoom = 3.0f;
            };

        if (!m_resource.Initialize(m_clientPath)) return false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(m_window.m_hWnd);
        ImGui_ImplDX11_Init((ID3D11Device*)m_renderer.GetD3DDevice(), (ID3D11DeviceContext*)m_renderer.GetD3DContext());

        OriginalWndProc = (WNDPROC)SetWindowLongPtr(m_window.m_hWnd, GWLP_WNDPROC, (LONG_PTR)HookWndProc);

        m_effectConfigs = m_resource.Parse3DEffects("ini\\3DEffect.ini");

        for (auto& pair : m_effectConfigs) {
            m_effectList.push_back(pair.first);
        }

        m_c3Paths = m_resource.ParseResIni("ini\\3dobj.ini");
        auto fxC3 = m_resource.ParseResIni("ini\\3DEffectObj.ini");
        m_c3Paths.insert(fxC3.begin(), fxC3.end());

        m_ddsPaths = m_resource.ParseResIni("ini\\3dtexture.ini");
        m_motionPaths = m_resource.ParseResIni("ini\\3Dmotion.ini");

        m_armorConfigs = m_resource.ParseArmorIni("ini\\armor.ini");
        m_armetConfigs = m_resource.ParseArmorIni("ini\\armet.ini");
        m_weaponConfigs = m_resource.ParseWeaponIni("ini\\weapon.ini");
        m_action3DEffects = m_resource.ParseAction3DEffects("ini\\Action3DEffect.ini");
        m_actionSounds = m_resource.ParseActionSound("ini\\ActionSound.ini");

        LoadItemTypes("ini\\itemtype.txt");
        LoadMonsterDB();
        LoadMagicDB();

        auto loadGuiFile = [&](const std::string& path) -> int {
            auto data = m_resource.GetFileData(path);
            if (!data.empty()) return m_renderer.LoadTextureFromMemory(data.data(), data.size());
            return -1;
            };

        m_texMainDialog1 = loadGuiFile("data\\main\\mainDialog1.dds");
        m_texMainDialog2 = loadGuiFile("data\\main\\mainDialog2.dds");
        m_texRunChk1 = loadGuiFile("data\\main\\RunChk1.dds");
        m_texRunChk2 = loadGuiFile("data\\main\\RunChk2.dds");

        m_texHpRed = Game::GenerateColorDDS(m_renderer, 220, 20, 20);
        m_texHpBlack = Game::GenerateColorDDS(m_renderer, 15, 15, 15);
        m_texHpOrange = Game::GenerateColorDDS(m_renderer, 255, 140, 0);

        auto [pTex, pW, pH] = Game::GenerateTextTexture(m_renderer, L"KayanK", RGB(255, 255, 255));
        m_player.nameTexId = pTex; m_player.nameW = pW; m_player.nameH = pH;

        std::string cursorPath = m_clientPath + "\\data\\Cursor\\Normal.ani";
        HCURSOR hCursor = LoadCursorFromFileA(cursorPath.c_str());
        if (hCursor) {
            SetClassLongPtr(m_window.m_hWnd, GCLP_HCURSOR, (LONG_PTR)hCursor);
            SetCursor(hCursor); ShowCursor(TRUE);
        }

        m_player.armorId = 0;
        m_currentArmetId = 0;
        m_currentGarmentId = 0;

        ChangeWeapon(0, 0);
        ChangeArmor(Game::ModelType::SmallFemale, m_player.armorId);
        ChangeArmet(Game::ModelType::SmallFemale, m_currentArmetId);
        ChangeGarment(Game::ModelType::SmallFemale, m_currentGarmentId);

        auto gameMaps = m_resource.LoadGameMapDat("ini\\GameMap.dat");
        if (gameMaps.count(1005)) {
            m_currentDMap = m_resource.LoadDMap(gameMaps[1005].dmapPath);
            if (m_currentDMap.isValid) {
                m_player.mapX = m_currentDMap.width / 2.0f;
                m_player.mapY = m_currentDMap.height / 2.0f;

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

    CachedMonster& GetMonsterRender(uint32_t meshId, uint32_t actionId) {
        uint64_t key = ((uint64_t)meshId << 32) | actionId;
        if (m_monsterCache.find(key) != m_monsterCache.end()) {
            return m_monsterCache[key];
        }

        CachedMonster render;
        uint32_t armorId = meshId * 1000000;

        if (m_armorConfigs.find(armorId) != m_armorConfigs.end()) {
            auto& armCfg = m_armorConfigs[armorId];
            if (!armCfg.parts.empty()) {
                auto& pCfg = armCfg.parts[0];
                render.asb = pCfg.asb;
                render.adb = pCfg.adb;

                if (m_c3Paths.count(pCfg.mesh)) {
                    render.model = m_resource.LoadC3Model(m_c3Paths[pCfg.mesh]);
                }
                if (m_ddsPaths.count(pCfg.texture)) {
                    auto texData = m_resource.GetFileData(m_ddsPaths[pCfg.texture]);
                    if (!texData.empty()) {
                        render.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                    }
                }
            }
        }

        uint32_t motionId = armorId + actionId;
        if (m_motionPaths.count(motionId)) {
            Resource::C3Model animModel = m_resource.LoadC3Model(m_motionPaths[motionId]);
            ApplyAnim(render.model, animModel);
        }
        else {
            uint32_t fallbackIdle = armorId + 100;
            if (m_motionPaths.count(fallbackIdle)) {
                Resource::C3Model animModel = m_resource.LoadC3Model(m_motionPaths[fallbackIdle]);
                ApplyAnim(render.model, animModel);
            }
        }

        m_monsterCache[key] = render;
        return m_monsterCache[key];
    }

    void LoadEffect(const std::string& effectName, float mapX = -1.0f, float mapY = -1.0f, float screenOffsetX = 0.0f, float screenOffsetY = 0.0f, bool isDamage = false, int overrideDelay = -1) {
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
        newActiveEffect.name = effectName;
        newActiveEffect.config = config;

        if (overrideDelay >= 0) {
            newActiveEffect.config.delay = overrideDelay;
        }

        newActiveEffect.mapX = mapX;
        newActiveEffect.mapY = mapY;
        newActiveEffect.screenOffsetX = screenOffsetX;
        newActiveEffect.screenOffsetY = screenOffsetY;

        newActiveEffect.baseOffsetY = screenOffsetY;
        newActiveEffect.isDamageNumber = isDamage;
        newActiveEffect.currentLife = 0.0f;
        newActiveEffect.maxLife = 2.0f;

        if (newActiveEffect.config.delay <= 0) {
            newActiveEffect.isWaitingDelay = false;
        }
        else {
            newActiveEffect.isWaitingDelay = true;
            newActiveEffect.currentTimer = 0.0f;
        }

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

    void LoadTME(const std::string& tmeFile, float startX, float startY, float angle) {
        if (tmeFile.empty() || tmeFile == "NULL") return;

        Resource::TMEData tmeParsed = m_resource.ParseTME("ini\\tme\\" + tmeFile);
        if (!tmeParsed.isValid) return;

        for (const auto& node : tmeParsed.nodes) {
            if (node.effectName.empty()) continue;

            float distTiles = node.distance / 40.0f;

            float rad = angle - 1.5708f;
            float targetX = startX + std::cos(rad) * distTiles;
            float targetY = startY + std::sin(rad) * distTiles;

            LoadEffect(node.effectName, targetX, targetY, 0.0f, 0.0f, false, node.delay);
        }
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

    void ChangeGarment(Game::ModelType type, uint32_t garmentId) {
        m_currentGarmentId = garmentId;
        for (auto& p : m_currentGarmentParts) { if (p.textureId != -1) m_renderer.DeleteTexture(p.textureId); }
        m_currentGarmentParts.clear();

        if (garmentId == 0) {
            ChangeArmor(m_player.modelType, m_player.armorId);
            return;
        }

        // [NOVO] Adicionado Fetching dos Modelos L/R separados de Andar e Correr (110 vs 120)
        Resource::C3Model animIdle = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::StandBy);
        Resource::C3Model animWalkL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)110);
        Resource::C3Model animWalkR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)111);
        Resource::C3Model animRunL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)120);
        Resource::C3Model animRunR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)121);
        Resource::C3Model animJump = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Jump);
        Resource::C3Model animAlert = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Alert);
        if (!animAlert.isValid || animAlert.motions.empty()) animAlert = animIdle;

        uint32_t modelPrefix = static_cast<uint32_t>(type);
        uint32_t baseGarmentId = (modelPrefix * 1000000) + garmentId;
        uint32_t finalGarmentId = baseGarmentId;

        if (m_armorConfigs.find(finalGarmentId) == m_armorConfigs.end()) {
            uint32_t fallback0 = (baseGarmentId / 10) * 10;
            if (m_armorConfigs.find(fallback0) != m_armorConfigs.end()) {
                finalGarmentId = fallback0;
            }
            else {
                uint32_t fallback5 = fallback0 + 5;
                if (m_armorConfigs.find(fallback5) != m_armorConfigs.end()) {
                    finalGarmentId = fallback5;
                }
                else {
                    return;
                }
            }
        }

        auto& armorCfg = m_armorConfigs[finalGarmentId];

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
                    if (m_currentGarmentParts.empty()) {
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
                    newPart.runModel = baseModel; ApplyWalkAnim(newPart.runModel, animRunL, animRunR); // [NOVO] Adicionado Corrida
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);

                    int attackTypes[] = { 401, 402, 403, 404, 405, 406, 407, 408, 409, 903 };
                    for (int at : attackTypes) {
                        Resource::C3Model aAnim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)at);
                        if (aAnim.isValid && !aAnim.motions.empty()) {
                            newPart.attackModels[at] = baseModel;
                            ApplyAnim(newPart.attackModels[at], aAnim);
                        }
                        else {
                            newPart.attackModels[at] = baseModel;
                            ApplyAnim(newPart.attackModels[at], GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_401));
                        }
                    }
                }
            }
            if (m_ddsPaths.find(pCfg.texture) != m_ddsPaths.end()) {
                std::string ddsPath = m_ddsPaths[pCfg.texture];
                auto texData = m_resource.GetFileData(ddsPath);
                if (!texData.empty()) {
                    newPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                }
            }
            m_currentGarmentParts.push_back(newPart);
        }
    }

    void ChangeArmet(Game::ModelType type, uint32_t armetId) {
        m_currentArmetId = armetId;

        Resource::C3Model animIdle = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::StandBy);
        Resource::C3Model animWalkL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)110);
        Resource::C3Model animWalkR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)111);
        Resource::C3Model animRunL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)120);
        Resource::C3Model animRunR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)121);
        Resource::C3Model animJump = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Jump);
        Resource::C3Model animAlert = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Alert);
        if (!animAlert.isValid || animAlert.motions.empty()) animAlert = animIdle;

        if (armetId == 0) {
            for (auto& p : m_currentArmetParts) {
                if (p.textureId != -1) m_renderer.DeleteTexture(p.textureId);
            }
            m_currentArmetParts.clear();
            return;
        }

        uint32_t modelPrefix = static_cast<uint32_t>(type);
        uint32_t baseArmetId = (modelPrefix * 1000000) + armetId;
        uint32_t finalArmetId = baseArmetId;

        if (m_armetConfigs.find(finalArmetId) == m_armetConfigs.end()) {
            uint32_t fallback0 = (baseArmetId / 10) * 10;
            if (m_armetConfigs.find(fallback0) != m_armetConfigs.end()) {
                finalArmetId = fallback0;
            }
            else {
                uint32_t fallback5 = fallback0 + 5;
                if (m_armetConfigs.find(fallback5) != m_armetConfigs.end()) {
                    finalArmetId = fallback5;
                }
                else {
                    finalArmetId = 0;
                }
            }
        }

        for (auto& p : m_currentArmetParts) {
            if (p.textureId != -1) m_renderer.DeleteTexture(p.textureId);
        }
        m_currentArmetParts.clear();

        if (finalArmetId == 0) return;

        auto& armetCfg = m_armetConfigs[finalArmetId];
        for (int i = 0; i < armetCfg.partCount; i++) {
            if (i >= armetCfg.parts.size()) break;
            auto& pCfg = armetCfg.parts[i];

            LoadedArmorPart newPart;
            newPart.asb = pCfg.asb;
            newPart.adb = pCfg.adb;

            if (m_c3Paths.find(pCfg.mesh) != m_c3Paths.end()) {
                std::string c3Path = m_c3Paths[pCfg.mesh];
                Resource::C3Model baseModel = m_resource.LoadC3Model(c3Path);

                if (baseModel.isValid) {
                    newPart.idleModel = baseModel; ApplyAnim(newPart.idleModel, animIdle);
                    newPart.walkModel = baseModel; ApplyWalkAnim(newPart.walkModel, animWalkL, animWalkR);
                    newPart.runModel = baseModel; ApplyWalkAnim(newPart.runModel, animRunL, animRunR); // [NOVO]
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);

                    int attackTypes[] = { 401, 402, 403, 404, 405, 406, 407, 408, 409, 903 };
                    for (int at : attackTypes) {
                        Resource::C3Model aAnim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)at);
                        if (aAnim.isValid && !aAnim.motions.empty()) {
                            newPart.attackModels[at] = baseModel;
                            ApplyAnim(newPart.attackModels[at], aAnim);
                        }
                        else {
                            newPart.attackModels[at] = baseModel;
                            ApplyAnim(newPart.attackModels[at], GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_401));
                        }
                    }
                }
            }
            if (m_ddsPaths.find(pCfg.texture) != m_ddsPaths.end()) {
                std::string ddsPath = m_ddsPaths[pCfg.texture];
                auto texData = m_resource.GetFileData(ddsPath);
                if (!texData.empty()) {
                    newPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                }
            }
            m_currentArmetParts.push_back(newPart);
        }
    }

    void ChangeWeapon(uint32_t rightId, uint32_t leftId) {
        m_player.rightHandWeaponId = rightId;
        m_player.leftHandWeaponId = leftId;

        auto loadWeaponParts = [&](uint32_t wid, std::vector<LoadedWeaponPart>& outParts) {
            for (auto& p : outParts) {
                if (p.textureId != -1) m_renderer.DeleteTexture(p.textureId);
                for (auto& ep : p.effectParts) if (ep.textureId != -1) m_renderer.DeleteTexture(ep.textureId);
            }
            outParts.clear();
            if (wid == 0) return;

            if (m_weaponConfigs.find(wid) != m_weaponConfigs.end()) {
                auto& wCfg = m_weaponConfigs[wid];
                for (auto& pCfg : wCfg.parts) {
                    LoadedWeaponPart newPart;
                    newPart.asb = pCfg.asb;
                    newPart.adb = pCfg.adb;

                    if (m_c3Paths.count(pCfg.mesh)) {
                        newPart.model = m_resource.LoadC3Model(m_c3Paths[pCfg.mesh]);
                    }
                    if (m_ddsPaths.count(pCfg.texture)) {
                        auto texData = m_resource.GetFileData(m_ddsPaths[pCfg.texture]);
                        if (!texData.empty()) {
                            newPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                        }
                    }

                    if (m_action3DEffects.find(wid) != m_action3DEffects.end()) {
                        std::string effName = m_action3DEffects[wid];
                        std::string lowerEffName = effName;
                        std::transform(lowerEffName.begin(), lowerEffName.end(), lowerEffName.begin(), ::tolower);

                        auto it = m_effectConfigs.find(lowerEffName);
                        if (it == m_effectConfigs.end()) it = m_effectConfigs.find(effName);

                        if (it != m_effectConfigs.end()) {
                            newPart.hasEffect = true;
                            newPart.effectConfig = it->second;

                            newPart.shapeStates.resize(it->second.parts.size());

                            for (auto& effCfgPart : newPart.effectConfig.parts) {
                                LoadedEffectPart ep;
                                if (m_c3Paths.count(effCfgPart.effectId)) {
                                    ep.model = m_resource.LoadC3Model(m_c3Paths[effCfgPart.effectId]);
                                }
                                if (m_ddsPaths.count(effCfgPart.textureId)) {
                                    auto td = m_resource.GetFileData(m_ddsPaths[effCfgPart.textureId]);
                                    if (!td.empty()) ep.textureId = m_renderer.LoadTextureFromMemory(td.data(), td.size());
                                }
                                newPart.effectParts.push_back(ep);
                            }
                        }
                    }

                    outParts.push_back(newPart);
                }
            }
            else {
                LoadedWeaponPart fallbackPart;
                if (m_c3Paths.find(wid) != m_c3Paths.end()) fallbackPart.model = m_resource.LoadC3Model(m_c3Paths[wid]);

                uint32_t texIdsToTry[] = { wid, wid + 9, wid + 8, wid + 5, wid + 1, wid + 2 };
                for (uint32_t tid : texIdsToTry) {
                    if (m_ddsPaths.find(tid) != m_ddsPaths.end()) {
                        auto texData = m_resource.GetFileData(m_ddsPaths[tid]);
                        if (!texData.empty()) {
                            fallbackPart.textureId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                            break;
                        }
                    }
                }
                outParts.push_back(fallbackPart);
            }
            };

        loadWeaponParts(rightId, m_rightWeaponParts);
        loadWeaponParts(leftId, m_leftWeaponParts);

        ChangeArmor(m_player.modelType, m_player.armorId);
        ChangeArmet(m_player.modelType, m_currentArmetId);
        ChangeGarment(m_player.modelType, m_currentGarmentId);
    }

    void ChangeArmor(Game::ModelType type, uint32_t armorId) {
        m_player.modelType = type;
        m_player.armorId = armorId;

        Resource::C3Model animIdle = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::StandBy);
        Resource::C3Model animWalkL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)110);
        Resource::C3Model animWalkR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)111);
        Resource::C3Model animRunL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)120);
        Resource::C3Model animRunR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)121);
        Resource::C3Model animJump = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Jump);

        Resource::C3Model animAlert = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Alert);
        if (!animAlert.isValid || animAlert.motions.empty()) animAlert = animIdle;

        ApplyAnim(m_hairIdleModel, animIdle);
        ApplyAnim(m_hairJumpModel, animJump);
        ApplyAnim(m_hairAlertModel, animAlert);
        ApplyWalkAnim(m_hairWalkModel, animWalkL, animWalkR);
        ApplyWalkAnim(m_hairRunModel, animRunL, animRunR); // [NOVO]

        int attackTypes[] = { 401, 402, 403, 404, 405, 406, 407, 408, 409, 903 };
        for (int at : attackTypes) {
            Resource::C3Model aAnim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)at);
            if (aAnim.isValid && !aAnim.motions.empty()) {
                m_hairAttackModels[at] = m_hairIdleModel;
                ApplyAnim(m_hairAttackModels[at], aAnim);
            }
            else {
                m_hairAttackModels[at] = m_hairIdleModel;
                ApplyAnim(m_hairAttackModels[at], GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_401));
            }
        }

        uint32_t modelPrefix = static_cast<uint32_t>(type);
        uint32_t baseArmorId = (modelPrefix * 1000000) + armorId;
        uint32_t finalArmorId = baseArmorId;

        if (m_armorConfigs.find(finalArmorId) == m_armorConfigs.end()) {
            uint32_t fallback0 = (baseArmorId / 10) * 10;
            if (m_armorConfigs.find(fallback0) != m_armorConfigs.end()) {
                finalArmorId = fallback0;
            }
            else {
                uint32_t fallback5 = fallback0 + 5;
                if (m_armorConfigs.find(fallback5) != m_armorConfigs.end()) {
                    finalArmorId = fallback5;
                }
                else {
                    finalArmorId = (modelPrefix * 1000000);
                }
            }
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
                    if (m_currentArmorParts.empty() && m_currentGarmentParts.empty()) {
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
                    newPart.runModel = baseModel;  ApplyWalkAnim(newPart.runModel, animRunL, animRunR); // [NOVO]
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);

                    for (int at : attackTypes) {
                        Resource::C3Model aAnim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)at);
                        if (aAnim.isValid && !aAnim.motions.empty()) {
                            newPart.attackModels[at] = baseModel;
                            ApplyAnim(newPart.attackModels[at], aAnim);
                        }
                        else {
                            newPart.attackModels[at] = baseModel;
                            ApplyAnim(newPart.attackModels[at], GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::PhysicalAttack_401));
                        }
                    }
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

    void EquipItemByID(uint32_t itemId) {
        if (itemId >= 130000 && itemId < 140000) {
            ChangeArmor(m_player.modelType, itemId);
        }
        else if (itemId >= 180000 && itemId <= 199999) {
            ChangeGarment(m_player.modelType, itemId);
        }
        else if ((itemId >= 110000 && itemId < 120000) || (itemId >= 140000 && itemId < 150000)) {
            ChangeArmet(m_player.modelType, itemId);
        }
        else if (itemId >= 400000 && itemId < 600000) {
            ChangeWeapon(itemId, m_player.leftHandWeaponId);
        }
        else if (itemId >= 900000 && itemId < 1000000) {
            ChangeWeapon(m_player.rightHandWeaponId, itemId);
        }
        else if (itemId >= 1050000 && itemId < 1060000) {
            ChangeWeapon(m_player.rightHandWeaponId, itemId);
        }
    }

    void DrawImGuiPanel() {
        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("Painel de Controle KayanK");

        if (ImGui::CollapsingHeader("Personagem (Modelo Base)")) {
            const char* sexNames[] = { "Mulher Pequena", "Mulher Alta", "Homem Pequeno", "Homem Alto" };
            int currentSex = (int)m_player.modelType - 1;
            if (currentSex < 0 || currentSex > 3) currentSex = 0;

            if (ImGui::Combo("Corpo", &currentSex, sexNames, 4)) {
                ChangeArmor((Game::ModelType)(currentSex + 1), m_player.armorId);
                ChangeArmet((Game::ModelType)(currentSex + 1), m_currentArmetId);
                ChangeGarment((Game::ModelType)(currentSex + 1), m_currentGarmentId);
            }
        }

        if (ImGui::CollapsingHeader("Equipamentos (ItemType.txt)")) {

            static int armet_idx = 0;
            if (!m_armetList.empty()) {
                if (ImGui::BeginCombo("Capacete (Armet)", m_armetList[armet_idx].name.c_str())) {
                    for (int n = 0; n < m_armetList.size(); n++) {
                        const bool is_selected = (armet_idx == n);
                        std::string displayText = std::to_string(m_armetList[n].id) + " - " + m_armetList[n].name;
                        if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                            armet_idx = n;
                            EquipItemByID(m_armetList[n].id);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            static int armor_idx = 0;
            if (!m_armorsList.empty()) {
                if (ImGui::BeginCombo("Armadura", m_armorsList[armor_idx].name.c_str())) {
                    for (int n = 0; n < m_armorsList.size(); n++) {
                        const bool is_selected = (armor_idx == n);
                        std::string displayText = std::to_string(m_armorsList[n].id) + " - " + m_armorsList[n].name;
                        if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                            armor_idx = n;
                            EquipItemByID(m_armorsList[n].id);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            static int garment_idx = 0;
            if (!m_garmentList.empty()) {
                if (ImGui::BeginCombo("Garment (Fantasia)", m_garmentList[garment_idx].name.c_str())) {
                    for (int n = 0; n < m_garmentList.size(); n++) {
                        const bool is_selected = (garment_idx == n);
                        std::string displayText = std::to_string(m_garmentList[n].id) + " - " + m_garmentList[n].name;
                        if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                            garment_idx = n;
                            EquipItemByID(m_garmentList[n].id);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            static int right_idx = 0;
            if (!m_rightWeaponList.empty()) {
                if (ImGui::BeginCombo("Mão Direita (R)", m_rightWeaponList[right_idx].name.c_str())) {
                    for (int n = 0; n < m_rightWeaponList.size(); n++) {
                        const bool is_selected = (right_idx == n);
                        std::string displayText = std::to_string(m_rightWeaponList[n].id) + " - " + m_rightWeaponList[n].name;
                        if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                            right_idx = n;
                            EquipItemByID(m_rightWeaponList[n].id);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            static int left_idx = 0;
            if (!m_leftWeaponList.empty()) {
                if (ImGui::BeginCombo("Mão Esquerda (L)", m_leftWeaponList[left_idx].name.c_str())) {

                    bool isRightBow = (m_player.rightHandWeaponId / 1000 == 500);

                    for (int n = 0; n < m_leftWeaponList.size(); n++) {
                        uint32_t lId = m_leftWeaponList[n].id;
                        uint32_t lPrefix = lId / 100000;
                        uint32_t lArrowPrefix = lId / 10000;

                        bool show = true;
                        if (isRightBow) {
                            if (lId != 0 && lArrowPrefix != 105) show = false;
                        }
                        else {
                            if (lArrowPrefix == 105) show = false;
                        }

                        if (show) {
                            const bool is_selected = (left_idx == n);
                            std::string displayText = std::to_string(lId) + " - " + m_leftWeaponList[n].name;
                            if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                                left_idx = n;
                                ChangeWeapon(m_player.rightHandWeaponId, lId);
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }

        if (ImGui::CollapsingHeader("Asas e Acessorios")) {
            static std::vector<std::string> wingNames;
            if (wingNames.empty()) {
                for (auto& pair : m_effectConfigs) {
                    std::string lowerName = pair.first;
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    if (lowerName.find("wing") != std::string::npos) {
                        wingNames.push_back(pair.first);
                    }
                }
            }
            static int wing_idx = 0;

            if (ImGui::Button("Remover Asa Atual", ImVec2(150.0f, 30.0f))) {
                ChangeWing("Nenhum");
            }
            ImGui::SameLine();
            if (ImGui::Button("Equipar Asa Selecionada", ImVec2(150.0f, 30.0f))) {
                if (!wingNames.empty()) ChangeWing(wingNames[wing_idx]);
            }

            if (ImGui::BeginCombo("Lista de Asas", wingNames.empty() ? "Carregando..." : wingNames[wing_idx].c_str())) {
                for (int n = 0; n < wingNames.size(); n++) {
                    const bool is_selected = (wing_idx == n);
                    if (ImGui::Selectable(wingNames[n].c_str(), is_selected)) {
                        wing_idx = n;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::CollapsingHeader("Magias (MagicType.txt)", ImGuiTreeNodeFlags_DefaultOpen)) {
            static int magic_idx = 0;

            if (!m_magicDB.empty()) {
                std::string currentMagicPreview = std::to_string(m_magicDB[magic_idx].id) + " - " + m_magicDB[magic_idx].name + " (Lv " + std::to_string(m_magicDB[magic_idx].level) + ")";

                if (ImGui::BeginCombo("Selecione a Magia", currentMagicPreview.c_str())) {
                    for (int n = 0; n < m_magicDB.size(); n++) {
                        const bool is_selected = (magic_idx == n);

                        std::string displayText = std::to_string(m_magicDB[n].id) + " - " + m_magicDB[n].name + " (Lv " + std::to_string(m_magicDB[n].level) + ")##" + std::to_string(n);

                        if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                            magic_idx = n;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Lancar Magia", ImVec2(-1.0f, 40.0f))) {
                    auto& magic = m_magicDB[magic_idx];

                    uint32_t finalAction = magic.actionId;
                    if (finalAction == 401) {
                        finalAction = 401 + (rand() % 3);
                    }

                    if (m_player.isMoving) {
                        m_player.isMoving = false;
                        m_player.targetMapX = m_player.mapX;
                        m_player.targetMapY = m_player.mapY;
                    }

                    if (m_player.isJumping) {
                        if (!magic.intoneEffect.empty()) LoadEffect(magic.intoneEffect, m_player.mapX, m_player.mapY);
                        if (!magic.senderEffect.empty()) LoadEffect(magic.senderEffect, m_player.mapX, m_player.mapY);
                        if (!magic.soundPath.empty()) m_audio.PlaySoundEffect(m_clientPath + "\\" + magic.soundPath);

                        if (!magic.tmeFile.empty()) LoadTME(magic.tmeFile, m_player.mapX, m_player.mapY, m_player.facingAngle);

                        if (!magic.targetEffect.empty() && magic.tmeFile.empty()) {
                            float rad = m_player.facingAngle - 1.5708f;
                            float fx = m_player.mapX + std::cos(rad) * 3.0f;
                            float fy = m_player.mapY + std::sin(rad) * 3.0f;
                            LoadEffect(magic.targetEffect, fx, fy);
                        }

                        m_player.hasQueuedAttack = true;
                        m_player.queuedAttackAnim = (Game::RoleActionType)finalAction;
                        m_player.queuedAttackIndex = finalAction;
                    }
                    else {
                        if (!m_player.isAttacking) {
                            m_player.isAttacking = true;
                            m_player.currentFrame = 0;
                            m_player.currentAttackIndex = finalAction;
                            m_player.currentAttackAnim = (Game::RoleActionType)finalAction;

                            PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), finalAction);
                        }

                        if (!magic.intoneEffect.empty()) LoadEffect(magic.intoneEffect, m_player.mapX, m_player.mapY);
                        if (!magic.senderEffect.empty()) LoadEffect(magic.senderEffect, m_player.mapX, m_player.mapY);

                        if (!magic.soundPath.empty()) {
                            m_audio.PlaySoundEffect(m_clientPath + "\\" + magic.soundPath);
                        }

                        if (!magic.tmeFile.empty()) {
                            LoadTME(magic.tmeFile, m_player.mapX, m_player.mapY, m_player.facingAngle);
                        }

                        if (!magic.targetEffect.empty() && magic.tmeFile.empty()) {
                            float rad = m_player.facingAngle - 1.5708f;
                            float fx = m_player.mapX + std::cos(rad) * 3.0f;
                            float fy = m_player.mapY + std::sin(rad) * 3.0f;
                            LoadEffect(magic.targetEffect, fx, fy);
                        }
                    }
                }
            }
            else {
                ImGui::Text("Banco de Dados MagicType.txt nao carregado.");
            }
        }

        if (ImGui::CollapsingHeader("Testar Todos Efeitos Visuais")) {
            static char searchBuffer[128] = "";
            ImGui::InputText("Buscar", searchBuffer, IM_ARRAYSIZE(searchBuffer));

            static int fx_idx = 0;
            std::string previewName = "Nenhum";
            if (!m_effectList.empty() && fx_idx >= 0 && fx_idx < m_effectList.size()) {
                previewName = m_effectList[fx_idx];
            }

            if (ImGui::BeginCombo("Banco de Dados de Efeitos", previewName.c_str())) {
                for (int n = 0; n < m_effectList.size(); n++) {
                    std::string effectName = m_effectList[n];
                    std::string searchStr = searchBuffer;

                    std::string lowerEffect = effectName;
                    std::string lowerSearch = searchStr;
                    std::transform(lowerEffect.begin(), lowerEffect.end(), lowerEffect.begin(), ::tolower);
                    std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);

                    if (searchStr.empty() || lowerEffect.find(lowerSearch) != std::string::npos) {
                        const bool is_selected = (fx_idx == n);
                        if (ImGui::Selectable(effectName.c_str(), is_selected)) {
                            fx_idx = n;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Disparar Efeito Bruto", ImVec2(-1.0f, 40.0f))) {
                if (!m_effectList.empty() && fx_idx >= 0 && fx_idx < m_effectList.size()) {
                    LoadEffect(m_effectList[fx_idx], m_player.mapX + 2.0f, m_player.mapY + 2.0f);
                }
            }
        }

        if (ImGui::CollapsingHeader("Monstros (Spawn)")) {
            static int monster_idx = 0;

            if (!m_monsterDB.empty()) {
                std::string currentMobPreview = std::to_string(m_monsterDB[monster_idx].id) + " - " + m_monsterDB[monster_idx].name;

                if (ImGui::BeginCombo("Selecione o Monstro", currentMobPreview.c_str())) {
                    for (int n = 0; n < m_monsterDB.size(); n++) {
                        const bool is_selected = (monster_idx == n);
                        std::string displayText = std::to_string(m_monsterDB[n].id) + " - " + m_monsterDB[n].name;

                        if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                            monster_idx = n;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Invocar Monstro (Summon)", ImVec2(-1.0f, 40.0f))) {
                    ExpandedMonsterEntity newMob;

                    newMob.mapX = m_player.mapX + (float)((rand() % 5) - 2);
                    newMob.mapY = m_player.mapY + (float)((rand() % 5) - 2);
                    newMob.startX = newMob.mapX;
                    newMob.startY = newMob.mapY;

                    newMob.meshId = m_monsterDB[monster_idx].meshId;
                    newMob.maxHp = m_monsterDB[monster_idx].maxLife;
                    newMob.hp = newMob.maxHp;
                    newMob.visualHp = (float)newMob.maxHp;
                    newMob.currentAction = 100;

                    std::wstring wideName(m_monsterDB[monster_idx].name.begin(), m_monsterDB[monster_idx].name.end());
                    auto [mTex, mW, mH] = Game::GenerateTextTexture(m_renderer, wideName, RGB(255, 255, 255));
                    newMob.nameTexId = mTex;
                    newMob.nameW = mW;
                    newMob.nameH = mH;

                    m_monsters.push_back(newMob);
                }
            }
            else {
                ImGui::Text("Banco de Dados dbmonster.txt nao carregado.");
            }
        }

        ImGui::End();
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

        ImGuiIO& io = ImGui::GetIO();

        POINT pt; GetCursorPos(&pt); ScreenToClient(m_window.m_hWnd, &pt);
        m_mouseX = pt.x; m_mouseY = pt.y;

        bool isMouseInside = (pt.x >= 0 && pt.x < m_window.m_width && pt.y >= 0 && pt.y < m_window.m_height);
        static bool s_prevLeftDown = false;
        bool currentLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool leftClicked = currentLeftDown && !s_prevLeftDown;
        s_prevLeftDown = currentLeftDown;

        if (!m_player.isAttacking && m_player.currentFrame == 0) {
            for (auto& w : m_rightWeaponParts) { for (auto& s : w.shapeStates) s.Reset(); }
            for (auto& w : m_leftWeaponParts) { for (auto& s : w.shapeStates) s.Reset(); }
        }

        if (m_player.isAlert && !m_player.isAttacking) {
            m_player.alertTimer -= deltaTime;
            if (m_player.alertTimer <= 0.0f) {
                m_player.isAlert = false;
                m_player.alertTimer = 0.0f;
            }
        }

        // [NOVO] Lógica de Interface vs Jogo (O Bloqueio do Click)
        bool mouseOnUI = false;
        int totalW = 1024;
        int startX = (m_window.m_width - totalW) / 2;
        int screenBottom = m_window.m_height;

        // Bounding Box da Botinha de Correr (Atualizado com as suas novas coordenadas!)
        int bootX = startX + 2;
        int bootY = screenBottom - 120;

        // Se o mouse estiver em cima da botinha (32x32)
        if (pt.x >= bootX && pt.x <= bootX + 32 && pt.y >= bootY && pt.y <= bootY + 32) {
            mouseOnUI = true; // Avisa que o mouse tá na interface
            if (leftClicked) {
                m_isRunning = !m_isRunning; // Troca entre Andar e Correr!
                leftClicked = false;        // "Mata" o clique aqui para ele não vazar pro mapa
            }
        }

        // Bounding Box Principal da Interface do Conquer (Para não andar por trás dela)
        if (pt.x >= startX && pt.x <= startX + 1024 && pt.y >= screenBottom - 128 && pt.y <= screenBottom) {
            mouseOnUI = true;
        }

        // Se o mouse estiver em cima da Interface, IGNORA A LÓGICA DO MAPA!
        if (hasFocus && isMouseInside && !io.WantCaptureMouse && !mouseOnUI) {
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

                            PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), 130);
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

                if (m_player.hasQueuedAttack) {
                    m_player.hasQueuedAttack = false;
                    m_player.isAttacking = true;
                    m_player.currentFrame = 0;
                    m_player.currentAttackIndex = m_player.queuedAttackIndex;
                    m_player.currentAttackAnim = m_player.queuedAttackAnim;

                    PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), m_player.queuedAttackIndex);

                    m_player.isAlert = true;
                    m_player.alertTimer = 5.0f;
                }
                else if (m_player.hasQueuedAction) {
                    m_player.hasQueuedAction = false;
                    float qdx = m_player.queuedTargetX - m_player.mapX;
                    float qdy = m_player.queuedTargetY - m_player.mapY;
                    float qdist = std::sqrt(qdx * qdx + qdy * qdy);
                    if (qdist > 0.05f) m_player.facingAngle = -(std::atan2(qdy, qdx) - 0.78539f);

                    if (m_player.queuedIsJump) {
                        m_player.isJumping = true; m_player.isMoving = false; m_player.jumpTimer = 0.0f;
                        m_player.startMapX = m_player.mapX; m_player.startMapY = m_player.mapY;
                        m_player.targetMapX = m_player.queuedTargetX; m_player.targetMapY = m_player.queuedTargetY;

                        PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), 130);
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

                    // [NOVO] Chasing respeita o botão de Correr/Andar!
                    float speed = m_isRunning ? 7.0f : 3.0f;
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
                        m_player.currentAttackIndex = 401 + (rand() % 3);
                        m_player.currentAttackAnim = (Game::RoleActionType)m_player.currentAttackIndex;

                        PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), m_player.currentAttackIndex);

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
                // [NOVO] Velocidade atrelada à botinha!
                float speed = m_isRunning ? 7.0f : 3.0f;
                m_player.mapX += (dx / dist) * speed * deltaTime;
                m_player.mapY += (dy / dist) * speed * deltaTime;
            }
        }

        m_player.animTimer += deltaTime;
        float currentAnimSpeed = 10.0f;

        // [NOVO] Ajuste de FPS da animação com base se está andando ou correndo
        if (m_player.isJumping) currentAnimSpeed = 15.0f;
        else if (m_player.isAttacking) currentAnimSpeed = 25.0f;
        else if (m_player.isMoving) currentAnimSpeed = m_isRunning ? 20.0f : 12.0f;

        if (m_player.animTimer >= (1.0f / currentAnimSpeed)) {
            m_player.currentFrame++;
            m_player.animTimer -= (1.0f / currentAnimSpeed);

            if (m_player.isMoving) {
                // [NOVO] Toca o som certo de corrida(120/121) ou passo(110/111)
                if (m_player.currentFrame % 14 == 0) {
                    PlayActionSound((uint32_t)m_player.modelType, 999, m_isRunning ? 120 : 110);
                }
                else if (m_player.currentFrame % 14 == 7) {
                    PlayActionSound((uint32_t)m_player.modelType, 999, m_isRunning ? 121 : 111);
                }
            }

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

                            PlayActionSound(mob.meshId, 999, 330);

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

        m_weaponEffectTimer += deltaTime * 1000.0f;
        if (m_weaponEffectTimer >= 33.0f) {
            m_weaponEffectFrame++;
            m_weaponEffectTimer -= 33.0f;
            if (m_weaponEffectFrame >= 30) m_weaponEffectFrame = 0;
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

                        auto& render = GetMonsterRender(monster.meshId, 330);
                        int maxFrames = 10;
                        if (render.model.isValid && !render.model.motions.empty()) {
                            maxFrames = render.model.motions[0].frameCount;
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
                        faisao.startX = faisao.mapX;
                        faisao.startY = faisao.mapY;
                        faisao.hp = 1000;
                        faisao.maxHp = 1000;
                        faisao.visualHp = 1000.0f;
                        auto [mTex, mW, mH] = Game::GenerateTextTexture(m_renderer, L"Pheasant", RGB(255, 255, 255));
                        faisao.nameTexId = mTex;
                        faisao.nameW = mW;
                        faisao.nameH = mH;

                        spawnedMonsters.push_back(faisao);
                        it = m_monsters.erase(it);
                        continue;
                    }

                    if (monster.animTimer >= (1.0f / 10.0f)) {
                        monster.currentFrame++;
                        monster.animTimer -= (1.0f / 10.0f);
                        auto& render = GetMonsterRender(monster.meshId, 331);
                        int maxFrames = 1;
                        if (render.model.isValid && !render.model.motions.empty()) {
                            maxFrames = render.model.motions[0].frameCount;
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
                    monster.mapX = monster.targetX;
                    monster.mapY = monster.targetY;
                    monster.isMoving = false;
                    monster.currentAction = 100;
                    monster.currentFrame = 0;
                    monster.waitTimer = 0.0f;
                    monster.timeToWait = 1.0f + (rand() % 4);
                }
                else {
                    monster.facingAngle = -(std::atan2(dy, dx) - 0.78539f);
                    float speed = 2.0f;
                    monster.mapX += (dx / dist) * speed * deltaTime;
                    monster.mapY += (dy / dist) * speed * deltaTime;
                }

                monster.animTimer += deltaTime;
                if (monster.animTimer >= (1.0f / 15.0f)) {
                    monster.currentFrame++;
                    monster.animTimer -= (1.0f / 15.0f);
                }
            }
            else {
                monster.waitTimer += deltaTime;
                if (monster.waitTimer >= monster.timeToWait) {
                    float offsetX = (float)((rand() % 9) - 4);
                    float offsetY = (float)((rand() % 9) - 4);
                    monster.targetX = monster.startX + offsetX;
                    monster.targetY = monster.startY + offsetY;
                    monster.isMoving = true;
                    monster.currentAction = 120;
                    monster.currentFrame = 0;
                }
                monster.animTimer += deltaTime;
                if (monster.animTimer >= (1.0f / 8.0f)) {
                    monster.currentFrame++;
                    monster.animTimer -= (1.0f / 8.0f);
                }
            }
            ++it;
        }

        for (auto it = m_activeEffects.begin(); it != m_activeEffects.end(); ) {
            auto& effect = *it;

            if (effect.isFirstFrame) {
                effect.isFirstFrame = false;
                ++it;
                continue;
            }

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

                    int maxFrames = 0;
                    for (const auto& part : effect.parts) {
                        if (part.model.isValid) {
                            if (!part.model.motions.empty()) maxFrames = (std::max)(maxFrames, part.model.motions[0].frameCount);
                            if (!part.model.ptcls.empty()) maxFrames = (std::max)(maxFrames, (int)part.model.ptcls[0].frames.size());
                        }
                    }
                    if (maxFrames <= 0) maxFrames = 30;

                    if (effect.currentFrame >= maxFrames) {
                        effect.loopCount++;

                        if (effect.config.loopTime != 0 && effect.loopCount >= effect.config.loopTime) {
                            effect.isFinished = true;
                        }
                        else {
                            effect.currentFrame = 0;
                            if (effect.config.loopInterval > 0) {
                                effect.isWaitingInterval = true;
                                effect.currentTimer = 0.0f;
                            }
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

                std::vector<LoadedArmorPart>* activeBodyParts = &m_currentArmorParts;
                if (!m_currentGarmentParts.empty()) {
                    activeBodyParts = &m_currentGarmentParts;
                }

                Resource::C3Model* mainBodyModel = nullptr;
                if (!activeBodyParts->empty()) {
                    if (m_player.isAttacking) {
                        if ((*activeBodyParts)[0].attackModels.count(m_player.currentAttackIndex))
                            mainBodyModel = &(*activeBodyParts)[0].attackModels[m_player.currentAttackIndex];
                        else
                            mainBodyModel = &(*activeBodyParts)[0].attackModels[401];
                    }
                    else if (m_player.isJumping) mainBodyModel = &(*activeBodyParts)[0].jumpModel;
                    // [NOVO] Alterna entre as animações do Corpo com base no m_isRunning
                    else if (m_player.isMoving) mainBodyModel = m_isRunning ? &(*activeBodyParts)[0].runModel : &(*activeBodyParts)[0].walkModel;
                    else if (m_player.isAlert) mainBodyModel = &(*activeBodyParts)[0].alertModel;
                    else mainBodyModel = &(*activeBodyParts)[0].idleModel;
                }

                for (auto& part : *activeBodyParts) {
                    Resource::C3Model* activeModel = &part.idleModel;

                    if (m_player.isAttacking) {
                        if (part.attackModels.count(m_player.currentAttackIndex))
                            activeModel = &part.attackModels[m_player.currentAttackIndex];
                        else
                            activeModel = &part.attackModels[401];
                    }
                    else if (m_player.isJumping) activeModel = &part.jumpModel;
                    // [NOVO] Alterna as partes secundárias (calça, bota)
                    else if (m_player.isMoving) activeModel = m_isRunning ? &part.runModel : &part.walkModel;
                    else if (m_player.isAlert) activeModel = &part.alertModel;

                    if (activeModel->isValid)
                        m_renderer.DrawMesh3D(*activeModel, cx, cy - (m_player.jumpZ * m_zoom), part.textureId, m_player.currentFrame, m_player.facingAngle, 0.0f, true, m_zoom, nullptr, -1, 0, part.asb, part.adb, 1.0f, false, 0);
                }

                if (m_currentArmetParts.empty()) {
                    Resource::C3Model* activeHair = &m_hairIdleModel;

                    if (m_player.isAttacking) {
                        if (m_hairAttackModels.count(m_player.currentAttackIndex))
                            activeHair = &m_hairAttackModels[m_player.currentAttackIndex];
                        else
                            activeHair = &m_hairAttackModels[401];
                    }
                    else if (m_player.isJumping) { activeHair = &m_hairJumpModel; }
                    // [NOVO] Alterna o cabelo
                    else if (m_player.isMoving) { activeHair = m_isRunning ? &m_hairRunModel : &m_hairWalkModel; }
                    else if (m_player.isAlert) { activeHair = &m_hairAlertModel; }

                    if (activeHair->isValid) m_renderer.DrawMesh3D(*activeHair, cx, cy - (m_player.jumpZ * m_zoom), m_hairTextureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom);
                }
                else {
                    for (auto& part : m_currentArmetParts) {
                        Resource::C3Model* activeModel = &part.idleModel;

                        if (m_player.isAttacking) {
                            if (part.attackModels.count(m_player.currentAttackIndex))
                                activeModel = &part.attackModels[m_player.currentAttackIndex];
                            else
                                activeModel = &part.attackModels[401];
                        }
                        else if (m_player.isJumping) activeModel = &part.jumpModel;
                        // [NOVO] Alterna o capacete
                        else if (m_player.isMoving) activeModel = m_isRunning ? &part.runModel : &part.walkModel;
                        else if (m_player.isAlert) activeModel = &part.alertModel;

                        if (activeModel->isValid)
                            m_renderer.DrawMesh3D(*activeModel, cx, cy - (m_player.jumpZ * m_zoom), part.textureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom, nullptr, -1, 0, part.asb, part.adb, 1.0f, false, 0);
                    }
                }

                int rightWeaponBone = m_rightHandPhy;
                int leftWeaponBone = m_leftHandPhy;

                if (m_player.rightHandWeaponId / 1000 == 500) {
                    rightWeaponBone = m_leftHandPhy;
                    leftWeaponBone = m_rightHandPhy;
                }

                for (auto& wPart : m_rightWeaponParts) {
                    if (wPart.model.isValid && mainBodyModel) {
                        m_renderer.DrawMesh3D(wPart.model, cx, cy - (m_player.jumpZ * m_zoom), wPart.textureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, rightWeaponBone, m_player.currentFrame, wPart.asb, wPart.adb, 1.0f, false, 0);

                        if (wPart.hasEffect) {
                            for (size_t i = 0; i < wPart.effectParts.size(); i++) {
                                auto& ep = wPart.effectParts[i];
                                if (ep.model.isValid) {
                                    int easb = wPart.effectConfig.parts[i].asb;
                                    int eadb = wPart.effectConfig.parts[i].adb;
                                    int eColor = wPart.effectConfig.colorEnable;

                                    if (!ep.model.phys.empty()) {
                                        int meshFrame = m_weaponEffectFrame;
                                        if (!ep.model.motions.empty()) meshFrame %= ep.model.motions[0].frameCount;
                                        m_renderer.DrawMesh3D(ep.model, cx, cy - (m_player.jumpZ * m_zoom), ep.textureId, meshFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, rightWeaponBone, m_player.currentFrame, easb, eadb, 1.0f, true, eColor);
                                    }

                                    if (!ep.model.ptcls.empty()) {
                                        int ptclFrame = m_weaponEffectFrame % ep.model.ptcls[0].frames.size();
                                        m_renderer.DrawParticles(ep.model, cx, cy - (m_player.jumpZ * m_zoom), ep.textureId, ptclFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, rightWeaponBone, m_player.currentFrame, eColor);
                                    }

                                    if (m_player.isAttacking && !ep.model.shapes.empty()) {
                                        m_renderer.DrawShapes(ep.model, wPart.shapeStates[i], cx, cy - (m_player.jumpZ * m_zoom), ep.textureId, m_weaponEffectFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, rightWeaponBone, m_player.currentFrame, eColor, false);
                                    }
                                }
                            }
                        }
                    }
                }

                for (auto& wPart : m_leftWeaponParts) {
                    if (wPart.model.isValid && mainBodyModel) {
                        m_renderer.DrawMesh3D(wPart.model, cx, cy - (m_player.jumpZ * m_zoom), wPart.textureId, m_player.currentFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, leftWeaponBone, m_player.currentFrame, wPart.asb, wPart.adb, 1.0f, false, 0);

                        if (wPart.hasEffect) {
                            for (size_t i = 0; i < wPart.effectParts.size(); i++) {
                                auto& ep = wPart.effectParts[i];
                                if (ep.model.isValid) {
                                    int easb = wPart.effectConfig.parts[i].asb;
                                    int eadb = wPart.effectConfig.parts[i].adb;
                                    int eColor = wPart.effectConfig.colorEnable;

                                    if (!ep.model.phys.empty()) {
                                        int meshFrame = m_weaponEffectFrame;
                                        if (!ep.model.motions.empty()) meshFrame %= ep.model.motions[0].frameCount;
                                        m_renderer.DrawMesh3D(ep.model, cx, cy - (m_player.jumpZ * m_zoom), ep.textureId, meshFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, leftWeaponBone, m_player.currentFrame, easb, eadb, 1.0f, true, eColor);
                                    }

                                    if (!ep.model.ptcls.empty()) {
                                        int ptclFrame = m_weaponEffectFrame % ep.model.ptcls[0].frames.size();
                                        m_renderer.DrawParticles(ep.model, cx, cy - (m_player.jumpZ * m_zoom), ep.textureId, ptclFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, leftWeaponBone, m_player.currentFrame, eColor);
                                    }

                                    if (m_player.isAttacking && !ep.model.shapes.empty()) {
                                        m_renderer.DrawShapes(ep.model, wPart.shapeStates[i], cx, cy - (m_player.jumpZ * m_zoom), ep.textureId, m_weaponEffectFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, leftWeaponBone, m_player.currentFrame, eColor, false);
                                    }
                                }
                            }
                        }
                    }
                }


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
                                int meshFrame = m_wingFrame;
                                if (!part.model.motions.empty()) meshFrame %= part.model.motions[0].frameCount;
                                m_renderer.DrawMesh3D(part.model, cx + wingOffsetX, cy - (m_player.jumpZ * m_zoom) - wingOffsetY, part.textureId, meshFrame, wingRotation, wingPitch, false, m_zoom, mainBodyModel, attachBone, m_player.currentFrame, asb, adb, 1.0f, true, 0);
                            }
                            if (!part.model.ptcls.empty()) {
                                int ptclFrame = m_wingFrame % part.model.ptcls[0].frames.size();
                                m_renderer.DrawParticles(part.model, cx + wingOffsetX, cy - (m_player.jumpZ * m_zoom) - wingOffsetY, part.textureId, ptclFrame, wingRotation, wingPitch, m_zoom, asb, adb, mainBodyModel, attachBone, m_player.currentFrame, 0);
                            }
                        }
                    }
                }
            }
            else if (node.type == 1) {
                auto& monster = m_monsters[node.index];

                auto& render = GetMonsterRender(monster.meshId, monster.currentAction);

                if (render.model.isValid) {
                    auto [mWorldX, mWorldY] = coordSystem.MapToScreen(monster.mapX, monster.mapY);
                    float drawX = mWorldX - m_cameraX; float drawY = mWorldY - m_cameraY;
                    float zX = cx + (drawX - cx) * m_zoom; float zY = cy + (drawY - cy) * m_zoom;

                    m_renderer.DrawMesh3D(render.model, zX, zY, render.textureId, monster.currentFrame, monster.facingAngle, 0.0f, false, m_zoom, nullptr, -1, 0, render.asb, render.adb, monster.alpha, false, 0);

                    if (!monster.isDead && m_texHpBlack != -1 && m_texHpRed != -1 && m_texHpOrange != -1) {
                        int mobHpBarW = 40; int mobHpBarH = 4;
                        int hpX = (int)zX - (int)((mobHpBarW * m_zoom) / 2);
                        int hpY = (int)zY - (int)(110 * m_zoom);

                        SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f); // Previne bugs no HP
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

                SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f); // Previne bugs nos objetos
                m_renderer.DrawSprite(obj.textureId, (int)zX, (int)zY, (int)zW, (int)zH);
            }
        }

        for (auto& effect : m_activeEffects) {
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
            float ePitch = 0.0f;

            if (effect.isDamageNumber) {
                ePitch = -1.5708f;
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

            int globalMaxFrames = 0;
            for (const auto& part : effect.parts) {
                if (part.model.isValid) {
                    if (!part.model.motions.empty()) globalMaxFrames = (std::max)(globalMaxFrames, part.model.motions[0].frameCount);
                    if (!part.model.ptcls.empty()) globalMaxFrames = (std::max)(globalMaxFrames, (int)part.model.ptcls[0].frames.size());
                }
            }
            if (globalMaxFrames <= 0) globalMaxFrames = 30;

            if (effect.config.loopTime != 0 && effect.currentFrame >= globalMaxFrames - 10) {
                float t = (float)(effect.currentFrame - (globalMaxFrames - 10)) / 10.0f;
                if (t > 1.0f) t = 1.0f;
                eAlpha *= (1.0f - t);
            }

            for (size_t i = 0; i < effect.parts.size(); i++) {
                auto& part = effect.parts[i];
                int asb = effect.config.parts[i].asb;
                int adb = effect.config.parts[i].adb;
                int eColor = effect.config.colorEnable;

                int passColorEnable = eColor;
                std::string effLower = effect.name;
                std::transform(effLower.begin(), effLower.end(), effLower.begin(), ::tolower);

                if (i == 0 && (effLower.find("zf2") != std::string::npos || effLower.find("thunder") != std::string::npos)) {
                    passColorEnable += 100;
                }

                if (part.model.isValid) {
                    if (!part.model.phys.empty()) {
                        bool drawMesh = true;
                        if (effect.config.loopTime != 0 && !part.model.motions.empty()) {
                            if (effect.currentFrame >= part.model.motions[0].frameCount) {
                                drawMesh = false;
                            }
                        }
                        if (drawMesh) {
                            m_renderer.DrawMesh3D(part.model, drawCx, drawCy, part.textureId, effect.currentFrame, 0.0f, ePitch, false, eScale, nullptr, -1, 0, asb, adb, eAlpha, true, passColorEnable);
                        }
                    }
                    if (!part.model.ptcls.empty()) {
                        bool drawPtcl = true;
                        if (effect.config.loopTime != 0 && effect.currentFrame >= part.model.ptcls[0].frames.size()) {
                            drawPtcl = false;
                        }
                        if (drawPtcl) {
                            int ptclFrame = effect.currentFrame;
                            if (effect.config.loopTime == 0) ptclFrame %= part.model.ptcls[0].frames.size();
                            for (auto& ptcl : part.model.ptcls) ptcl.globalAlpha = eAlpha;
                            m_renderer.DrawParticles(part.model, drawCx, drawCy, part.textureId, ptclFrame, 0.0f, ePitch, eScale, asb, adb, nullptr, -1, 0, eColor);
                        }
                    }
                }
            }
        }
    }

    void DrawGUI() {
        int totalW = 1024;
        int startX = (m_window.m_width - totalW) / 2;
        int screenBottom = m_window.m_height;

        if (m_texMainDialog1 != -1 && m_texMainDialog2 != -1) {
            SetSpriteUV(0.0f, 0.5f, 1.0f, 1.0f);
            m_renderer.DrawSprite(m_texMainDialog1, startX, screenBottom - 128, 256, 128);

            SetSpriteUV(0.0f, 0.0f, 1.0f, 0.25f);
            m_renderer.DrawSprite(m_texMainDialog1, startX + 256, screenBottom - 53, 256, 64);

            SetSpriteUV(0.0f, 0.0f, 1.0f, 0.5f);
            m_renderer.DrawSprite(m_texMainDialog2, startX + 512, screenBottom - 53, 256, 64);

            SetSpriteUV(0.0f, 0.5f, 1.0f, 1.0f);
            m_renderer.DrawSprite(m_texMainDialog2, startX + 768, screenBottom - 54, 256, 64);
        }

        // [NOVO] Desenhando a Botinha de Correr/Andar dinamicamente!
        if (m_texRunChk1 != -1 && m_texRunChk2 != -1) {
            int bootX = startX + 2;
            int bootY = screenBottom - 120;
            SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
            m_renderer.DrawSprite(m_isRunning ? m_texRunChk2 : m_texRunChk1, bootX, bootY, 32, 32);
        }

        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        wchar_t debugBuffer[256];

        swprintf(debugBuffer, 256, L"[KayanK-CO] (%d, %d) Ping: 0, Fps: %d, %02d:%02d %02d/%02d/%04d",
            (int)m_player.mapX, (int)m_player.mapY,
            m_currentFps,
            localTime->tm_hour, localTime->tm_min, localTime->tm_mday, localTime->tm_mon + 1, localTime->tm_year + 1900);

        std::wstring currentDebugStr(debugBuffer);

        if (currentDebugStr != m_lastDebugStr) {
            if (m_debugTexId != -1) m_renderer.DeleteTexture(m_debugTexId);
            auto [tId, tW, tH] = Game::GenerateTextTexture(m_renderer, currentDebugStr, RGB(255, 255, 255));
            m_debugTexId = tId; m_debugTexW = tW; m_debugTexH = tH;
            m_lastDebugStr = currentDebugStr;
        }

        SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
        if (m_debugTexId != -1) m_renderer.DrawSprite(m_debugTexId, 10, 10, m_debugTexW, m_debugTexH);
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

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                DrawImGuiPanel();

                ImGui::Render();
                DrawWorld(); DrawGUI();

                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                m_renderer.EndFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    AllocConsole(); FILE* dummy; freopen_s(&dummy, "CONOUT$", "w", stdout);
    Application app; if (app.Initialize(hInstance)) app.Run();
    return 0;
}