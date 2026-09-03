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
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib> 
#include <ctime>   
#include <sstream>
#include <tuple> 
#include <map>
#include <filesystem>

#include "../Graphics/Graphics.h"
#include "../Graphics/Graphics_D3D.h"
#include "../Resource/Resource.h"
#include "../Resource/Resource_Utils.h"
#include "../Audio/Audio.h"
#include "../Graphics_Riot/Graphics_Riot.h"

#include "Engine_Window.h"
#include "Engine_Math.h"
#include "Game_Entities.h"
#include "Game_Utils.h"

#include <DirectXMath.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <commdlg.h>

// Abre um dialogo padrao do Windows para escolher um arquivo. filter segue o formato do
// OPENFILENAME (ex.: L"Arquivos SKN (*.skn)\0*.skn\0Todos os arquivos (*.*)\0*.*\0").
// Retorna true e preenche outPath se o usuario confirmou a selecao.
static bool OpenFileDialog(HWND owner, const wchar_t* filter, std::wstring& outPath) {
    wchar_t buffer[MAX_PATH] = {};
    if (!outPath.empty()) {
        wcsncpy_s(buffer, outPath.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        outPath = buffer;
        return true;
    }
    return false;
}

// [A PONTE INDESTRUTÍVEL DO LINKER] Importa a função Pura C global do Graphics.cpp
extern "C" void SetSpriteUV(float u1, float v1, float u2, float v2);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC OriginalWndProc = nullptr;

LRESULT CALLBACK HookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
}

// Computes the effective frame count of a C3Model for effect looping purposes.
// Mirrors C3Studio's per-mesh timelines: some PHY meshes have no MOTI (bone
// animation) or PTCL (particles) at all and are driven purely by their own
// alpha/draw/changeTex keyframe tracks (e.g. tornado part 1, an atlas-only
// mesh). Without considering those tracks here, such parts fell back to a
// hardcoded 30-frame loop and got cut off mid-cycle.
static int GetModelMaxFrame(const Resource::C3Model& model) {
    int maxFrames = 0;
    for (const auto& motion : model.motions) maxFrames = (std::max)(maxFrames, motion.frameCount);
    for (const auto& ptcl : model.ptcls) maxFrames = (std::max)(maxFrames, (int)ptcl.frames.size());
    for (const auto& phy : model.phys) {
        for (const auto& k : phy.alphaKeys) maxFrames = (std::max)(maxFrames, k.frame);
        for (const auto& k : phy.drawKeys) maxFrames = (std::max)(maxFrames, k.frame);
        for (const auto& k : phy.changeTexKeys) maxFrames = (std::max)(maxFrames, k.frame);
    }
    return maxFrames;
}

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
    std::string m_clientPath = "D:\\projetos\\kayank\\5017\\cliente";
    std::string m_currentEffectName = "Nenhum";
    int m_splashTexId = -1; // [Tela de carregamento] data\main\LogoN.bmp exibida durante o Initialize()

    // [Config.ini] Configuracoes de tela/desempenho lidas de "config.ini" (mesma pasta do .exe).
    // Fullscreen=0 usa modo janela; Fullscreen=1 usa tela cheia sem janela (WS_POPUP no tamanho
    // da tela primaria). FPSLimit=0 significa sem limite (apenas o VSync, se ativado, regula).
    int m_configWidth = 1024;
    int m_configHeight = 768;
    bool m_configFullscreen = false;
    bool m_configVSync = true;
    int m_configFpsLimit = 60;

    // [Config.ini] Le "config.ini" (arquivo texto simples, no formato [Secao]\nChave=Valor) da
    // pasta de trabalho do executavel (ex.: x64\Debug\config.ini). Se o arquivo ou uma chave nao
    // existir, mantem os valores padrao acima/em m_clientPath.
    void LoadConfigIni(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line, section;
        while (std::getline(file, line)) {
            // Remove comentarios (";" ou "#") e espacos nas pontas.
            size_t commentPos = line.find_first_of(";#");
            if (commentPos != std::string::npos) line = line.substr(0, commentPos);

            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);
            if (line.empty()) continue;

            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);

            size_t keyEnd = key.find_last_not_of(" \t");
            if (keyEnd != std::string::npos) key = key.substr(0, keyEnd + 1);
            size_t valStart = value.find_first_not_of(" \t");
            value = (valStart == std::string::npos) ? "" : value.substr(valStart);

            if (section == "Client" && key == "ClientPath") {
                if (!value.empty()) m_clientPath = value;
            }
            else if (section == "Display") {
                if (key == "Width") m_configWidth = std::atoi(value.c_str());
                else if (key == "Height") m_configHeight = std::atoi(value.c_str());
                else if (key == "Fullscreen") m_configFullscreen = (std::atoi(value.c_str()) != 0);
                else if (key == "VSync") m_configVSync = (std::atoi(value.c_str()) != 0);
                else if (key == "FPSLimit") m_configFpsLimit = std::atoi(value.c_str());
            }
        }
    }

    // Retorna o diretorio onde o .exe esta localizado (nao o diretorio de trabalho atual, que no
    // Visual Studio ao rodar com F5 normalmente e a pasta do projeto, e nao a pasta de saida onde
    // fica o .exe e o lol_personagens.ini).
    static std::string GetExeDirectory() {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len == 0) return std::string();
        std::filesystem::path exePath(buffer);
        std::string dir = exePath.parent_path().string();
        if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += '\\';
        return dir;
    }

    // Resolve um caminho relativo (ex.: "lol_personagens.ini") para o diretorio do .exe, garantindo
    // que o arquivo seja encontrado independente do diretorio de trabalho atual (CWD).
    static std::string ResolvePathNextToExe(const std::string& relativeOrAbsolute) {
        if (relativeOrAbsolute.size() >= 2 &&
            (relativeOrAbsolute[1] == ':' || (relativeOrAbsolute[0] == '\\' && relativeOrAbsolute[1] == '\\'))) {
            return relativeOrAbsolute; // ja e um caminho absoluto
        }
        return GetExeDirectory() + relativeOrAbsolute;
    }

    // Faz split de uma string separada por virgulas (com espacos opcionais apos a virgula),
    // usado para ler "nomes=" do lol_personagens.ini.
    struct RiotIniAnimEntry { std::string name; std::string path; };
    static std::vector<std::string> SplitCommaList(const std::string& value) {
        std::vector<std::string> result;
        size_t pos = 0;
        while (pos <= value.size()) {
            size_t comma = value.find(',', pos);
            std::string token = (comma == std::string::npos) ? value.substr(pos) : value.substr(pos, comma - pos);
            size_t s = token.find_first_not_of(" \t\r\n");
            size_t e = token.find_last_not_of(" \t\r\n");
            if (s != std::string::npos) result.push_back(token.substr(s, e - s + 1));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return result;
    }

    // [lol_personagens.ini] Novo formato simplificado: apenas [champs]/nomes=aatrox,ahri,...
    // A partir do nome do champ, a lista de skins e os arquivos de cada skin sao descobertos
    // diretamente na pasta de assets (ver ScanRiot2Skins/ScanRiot2SkinFiles), sem depender mais
    // de caminhos gravados no .ini.
    void LoadRiotChampionsIni(const std::string& path) {
        std::string resolvedPath = ResolvePathNextToExe(path);
        std::ifstream file(resolvedPath);
        if (!file.is_open()) {
            std::cout << "[Riot2] Nao foi possivel abrir " << resolvedPath << std::endl;
            return;
        }

        std::string line, section;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();


            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);

            if (section == "champs" && key == "nomes") {
                m_riot2ChampList = SplitCommaList(value);
            }
        }

        std::sort(m_riot2ChampList.begin(), m_riot2ChampList.end());
        std::cout << "[Riot2] lol_personagens.ini carregado: " << m_riot2ChampList.size() << " campeoes." << std::endl;
    }

    // Lista as subpastas de "assets/characters/<champ>/skins/" (ex.: base, skin01, skin02...),
    // com "base" sempre primeiro.
    std::vector<std::string> ScanRiot2Skins(const std::string& champ) {
        std::vector<std::string> skins;
        std::string skinsDir = m_riotAssetsRoot + "assets/characters/" + champ + "/skins/";
        std::error_code ec;
        if (!std::filesystem::exists(skinsDir, ec) || !std::filesystem::is_directory(skinsDir, ec)) return skins;

        for (const auto& entry : std::filesystem::directory_iterator(skinsDir, ec)) {
            if (!entry.is_directory()) continue;
            skins.push_back(entry.path().filename().string());
        }
        std::sort(skins.begin(), skins.end(), [](const std::string& a, const std::string& b) {
            if (a == "base") return true;
            if (b == "base") return false;
            return a < b;
            });
        return skins;
    }

    // Varre a pasta de uma skin especifica e devolve os arquivos encontrados: skn/skl (0 ou 1
    // cada), todas as texturas (.tex/.dds, incluindo as usadas por meshes extras como armas/props)
    // e todas as animacoes (.anm) dentro da subpasta "animations/".
    struct RiotSkinFiles {
        std::string sknPath;   // relativo a m_riotAssetsRoot
        std::string sklPath;
        std::vector<std::string> texPaths; // relativos a m_riotAssetsRoot
        std::vector<RiotIniAnimEntry> anims; // name = nome do arquivo, path = relativo a m_riotAssetsRoot
    };

    RiotSkinFiles ScanRiot2SkinFiles(const std::string& champ, const std::string& skin) {
        RiotSkinFiles result;
        std::string relBase = "assets/characters/" + champ + "/skins/" + skin + "/";
        std::string fullBase = m_riotAssetsRoot + relBase;
        std::error_code ec;
        if (!std::filesystem::exists(fullBase, ec) || !std::filesystem::is_directory(fullBase, ec)) return result;

        for (const auto& entry : std::filesystem::directory_iterator(fullBase, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            std::string relPath = relBase + entry.path().filename().string();

            if (ext == ".skn" && result.sknPath.empty()) result.sknPath = relPath;
            else if (ext == ".skl" && result.sklPath.empty()) result.sklPath = relPath;
            else if (ext == ".tex" || ext == ".dds") result.texPaths.push_back(relPath);
        }

        std::string animsDir = fullBase + "animations/";
        if (std::filesystem::exists(animsDir, ec) && std::filesystem::is_directory(animsDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(animsDir, ec)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (ext != ".anm") continue;
                std::string fileName = entry.path().filename().string();
                result.anims.push_back({ fileName, relBase + "animations/" + fileName });
            }
        }
        std::sort(result.texPaths.begin(), result.texPaths.end());
        std::sort(result.anims.begin(), result.anims.end(), [](const RiotIniAnimEntry& a, const RiotIniAnimEntry& b) { return a.name < b.name; });
        return result;
    }

    // Resolve os arquivos finais (skn/skl/texturas/animacoes) para o champ+skin escolhidos no
    // painel "Riot Champion 2 (LoL)", varrendo as pastas diretamente. Se a skin escolhida nao
    // tiver skn/skl/anm proprios (comum em skins alternativas), usa os da skin "base" do mesmo
    // champ, mas mantem as texturas encontradas na pasta da skin escolhida.
    void ResolveRiot2Selection() {
        m_riot2ResolvedSkn.clear();
        m_riot2ResolvedSkl.clear();
        m_riot2ResolvedTexPaths.clear();
        m_riot2ResolvedAnims.clear();

        if (m_riot2ChampIdx < 0 || m_riot2ChampIdx >= (int)m_riot2ChampList.size()) return;
        const std::string& champ = m_riot2ChampList[m_riot2ChampIdx];

        auto skinsIt = m_riot2SkinsByChamp.find(champ);
        if (skinsIt == m_riot2SkinsByChamp.end()) {
            m_riot2SkinsByChamp[champ] = ScanRiot2Skins(champ);
            skinsIt = m_riot2SkinsByChamp.find(champ);
        }
        if (skinsIt == m_riot2SkinsByChamp.end() || skinsIt->second.empty()) return;
        if (m_riot2SkinIdx < 0 || m_riot2SkinIdx >= (int)skinsIt->second.size()) m_riot2SkinIdx = 0;
        const std::string& skin = skinsIt->second[m_riot2SkinIdx];

        RiotSkinFiles selFiles = ScanRiot2SkinFiles(champ, skin);
        RiotSkinFiles baseFiles = (skin != "base") ? ScanRiot2SkinFiles(champ, "base") : selFiles;

        m_riot2ResolvedSkn = !selFiles.sknPath.empty() ? selFiles.sknPath : baseFiles.sknPath;
        m_riot2ResolvedSkl = !selFiles.sklPath.empty() ? selFiles.sklPath : baseFiles.sklPath;
        m_riot2ResolvedAnims = !selFiles.anims.empty() ? selFiles.anims : baseFiles.anims;
        // Texturas: sempre prioriza as encontradas na pasta da skin escolhida (mesmo quando skn/skl
        // vem da base), pois cada skin traz suas proprias texturas mesmo sem skn/skl proprios.
        m_riot2ResolvedTexPaths = !selFiles.texPaths.empty() ? selFiles.texPaths : baseFiles.texPaths;

        if (m_riot2AnimIdx < 0 || m_riot2AnimIdx >= (int)m_riot2ResolvedAnims.size()) m_riot2AnimIdx = 0;
    }

    void UpdateRiot2NameTexture() {
        if (m_riot2NameTexId != -1) {
            m_renderer.DeleteTexture(m_riot2NameTexId);
            m_riot2NameTexId = -1;
        }
        if (m_riot2Name.empty()) return;
        std::wstring wname(m_riot2Name.begin(), m_riot2Name.end());
        auto texData = Game::GenerateTextTexture(m_renderer, wname, RGB(0, 255, 255));
        m_riot2NameTexId = std::get<0>(texData);
        m_riot2NameW = std::get<1>(texData);
        m_riot2NameH = std::get<2>(texData);
    }

    // Constroi a lista de nomes de submesh atual do modelo Riot2 (apos aplicar/recarregar),
    // junto com o estado de visibilidade e a textura selecionada para cada um (indice em
    // m_riot2ResolvedTexPaths, -1 = usa a textura padrao do modelo).
    void RefreshRiot2SubMeshUiState() {
        m_riot2SubMeshTexChoice.clear();
        int count = GraphicsRiot::GetRiotSubMeshCount(m_riot2Handle);
        m_riot2SubMeshTexChoice.resize(count, -1);
    }

    void ApplyRiot2Champion() {
        if (GraphicsRiot::IsRiotModelValid(m_riot2Handle)) {
            GraphicsRiot::UnloadRiotChampion(m_riot2Handle);
            m_riot2Handle = GraphicsRiot::InvalidRiotModelHandle;
        }

        ResolveRiot2Selection();

        if (m_riot2ResolvedSkn.empty() || m_riot2ResolvedSkl.empty()) {
            std::cout << "[Riot2] Nao foi possivel aplicar: skn/skl nao encontrados para a selecao atual." << std::endl;
            return;
        }

        std::string sknFull = m_riotAssetsRoot + m_riot2ResolvedSkn;
        std::string sklFull = m_riotAssetsRoot + m_riot2ResolvedSkl;
        std::string anmFull;
        if (!m_riot2ResolvedAnims.empty() && m_riot2AnimIdx >= 0 && m_riot2AnimIdx < (int)m_riot2ResolvedAnims.size()) {
            anmFull = m_riotAssetsRoot + m_riot2ResolvedAnims[m_riot2AnimIdx].path;
        }
        // Textura padrao (compartilhada por todos os submeshes que nao tiverem override): a
        // primeira textura encontrada na pasta da skin.
        std::string texFullUtf8 = m_riot2ResolvedTexPaths.empty() ? std::string() : (m_riotAssetsRoot + m_riot2ResolvedTexPaths[0]);
        std::wstring texFull(texFullUtf8.begin(), texFullUtf8.end());

        m_riot2Handle = GraphicsRiot::LoadRiotChampion(sknFull.c_str(), sklFull.c_str(), anmFull.c_str(), texFull.c_str());

        if (!GraphicsRiot::IsRiotModelValid(m_riot2Handle)) {
            std::cout << "[Riot2] Falha ao aplicar o champion selecionado (" << sknFull << ")." << std::endl;
        }
        else {
            std::cout << "[Riot2] Champion aplicado com sucesso: " << sknFull << std::endl;
            RefreshRiot2SubMeshUiState();
        }
    }

    void RemoveRiot2Champion() {
        if (GraphicsRiot::IsRiotModelValid(m_riot2Handle)) {
            GraphicsRiot::UnloadRiotChampion(m_riot2Handle);
        }
        m_riot2Handle = GraphicsRiot::InvalidRiotModelHandle;
        m_riot2Enabled = false;
        m_riot2SubMeshTexChoice.clear();
    }

    Engine::WindowManager m_window;
    Graphics::SceneRenderer m_renderer;
    Resource::Manager m_resource;
    Audio::Manager m_audio;

    // [TESTE] Champion do LoL carregado como monstro via Graphics_Riot (.skn/.skl/.anm).
    GraphicsRiot::RiotModelHandle m_riotTestHandle = GraphicsRiot::InvalidRiotModelHandle;

    // Controle via ImGui: caminhos dos arquivos e transform (angulo/escala) do champion Riot.
    std::string m_riotSknPath;
    std::string m_riotSklPath;
    std::string m_riotAnmPath;
    std::wstring m_riotTexPath;
    float m_riotAngleDeg = 0.0f;
    float m_riotScale = 1.0f;

    // Nome exibido acima do champion Riot e sua barra de vida (cor azul, definida via ImGui).
    std::string m_riotName;
    int m_riotNameTexId = -1;
    int m_riotNameW = 0;
    int m_riotNameH = 0;
    float m_riotHpRatio = 1.0f; // 0.0 - 1.0
    int m_texHpBlueRiot = -1;

    // ------------------------------------------------------------------
    // "Riot Champion 2 (LoL)": segundo slot de campeao, carregado a partir
    // do catalogo lol_personagens.ini (apenas [champs]/nomes=aatrox,ahri,...)
    // com skins/arquivos/animacoes descobertos diretamente nas pastas de
    // assets/characters/<champ>/skins/<skin>/, ao inves de caminhos manuais.
    // ------------------------------------------------------------------
    std::string m_riotAssetsRoot = "F:\\Coisas lol\\novo_tudo\\Game\\";
    std::vector<std::string> m_riot2ChampList; // nomes unicos de champ (ex.: "aatrox")
    std::map<std::string, std::vector<std::string>> m_riot2SkinsByChamp; // "aatrox" -> ["base","skin01",...] (cache de scan)

    GraphicsRiot::RiotModelHandle m_riot2Handle = GraphicsRiot::InvalidRiotModelHandle;
    bool m_riot2Enabled = false;
    int m_riot2ChampIdx = 0;
    int m_riot2SkinIdx = 0;
    int m_riot2AnimIdx = 0;
    float m_riot2AngleDeg = 0.0f;
    float m_riot2Scale = 1.0f;
    std::string m_riot2Name;
    int m_riot2NameTexId = -1;
    int m_riot2NameW = 0;
    int m_riot2NameH = 0;
    float m_riot2HpRatio = 1.0f;
    // Resolved (com fallback para a skin "base" quando skn/skl/anm nao existirem na skin escolhida).
    std::string m_riot2ResolvedSkn, m_riot2ResolvedSkl;
    std::vector<std::string> m_riot2ResolvedTexPaths; // todas as texturas .tex/.dds da pasta da skin (objeto0 = padrao)
    std::vector<RiotIniAnimEntry> m_riot2ResolvedAnims;
    // Para cada submesh do modelo carregado, indice em m_riot2ResolvedTexPaths escolhido no ImGui
    // (-1 = usa a textura padrao do modelo, ou seja, m_riot2ResolvedTexPaths[0]).
    std::vector<int> m_riot2SubMeshTexChoice;

    Game::PlayerEntity m_player;
    uint32_t m_currentArmetId = 0;
    uint32_t m_currentGarmentId = 0;

    // Sons ambiente / musica por regiao do mapa (ini\MusicRegion.ini)
    std::vector<Resource::MusicRegionEntry> m_musicRegions;
    int m_activeMusicRegion = -1;      // indice em m_musicRegions da regiao ativa (-1 = nenhuma)
    int m_activeMusicIndex = -1;       // indice da faixa MusicN tocando atualmente dentro da regiao (-1 = tocando TitleMusic)
    float m_musicTrackTimer = 0.0f;    // tempo restante da faixa/titulo atual antes de avancar
    float m_musicDelayTimer = 0.0f;    // atraso pendente entre faixas (DelayTime)
    bool m_musicDelayPending = false;

    // Regioes de nome/efeito do mapa (ini\region.ini): nome discreto sobre o minimap
    // e efeito 3D acionado enquanto o player estiver dentro do range x,y -> x+cx,y+cy.
    std::vector<Resource::MapRegionEntry> m_mapRegions;
    int m_activeRegionNameIdx = -1;   // regiao (com regionName) que definiu o texto atual
    int m_regionNameTexId = -1;
    int m_regionNameW = 0, m_regionNameH = 0;
    int m_activeRegionEffectIdx = -1; // regiao (com effectName) cujo efeito esta ativo

    struct ExpandedMonsterEntity : public Game::MonsterEntity {
        uint32_t meshId;
        uint32_t originGeneratorId = 0; // 0 = sem gerador (ex.: invocado manualmente pelo debug)
        uint64_t uid = 0;               // identificador unico (usado para o efeito de nascimento seguir o monstro)
        bool hasPlayedBornAction = false;
    };
    uint64_t m_nextMonsterUid = 1;
    std::vector<ExpandedMonsterEntity> m_monsters;
    std::vector<Game::SceneObject> m_sceneObjects;

    Resource::C3Model m_hairIdleModel, m_hairWalkModel, m_hairRunModel, m_hairJumpModel, m_hairAlertModel, m_hairSwimModel;
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
        float angle = 0.0f;

        bool isDamageNumber = false;
        float currentLife = 0.0f;
        float maxLife = 2.0f;
        float baseOffsetY = 0.0f;

        bool isFirstFrame = true;
        bool isScreenFixed = false; // ignora jumpZ e fica travado no centro da tela (ex: efeito de regiao/cidade)

        // [Efeito preso ao monstro] Quando != 0, mapX/mapY sao recalculados a cada frame
        // a partir da posicao atual do monstro dono (ex.: BornEffect do Monster.txt), entao
        // se o monstro se mover o efeito acompanha.
        uint64_t attachedMonsterUid = 0;
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
        Resource::C3Model runModel;
        Resource::C3Model jumpModel;
        Resource::C3Model alertModel;
        Resource::C3Model swimModel;
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

        // [ini\Monster.txt] Acao/efeito/som tocados uma unica vez ao nascer o monstro.
        uint32_t bornAction = 100; // ex.: 315 (senao definido, fica em StandBy)
        std::string bornEffect;    // nome do efeito em ini\3DEffect.ini (ex.: MBStandard)
        std::string bornSound;     // geralmente "none"
    };
    std::vector<MonsterDef> m_monsterDB;

    struct MagicDef {
        uint32_t id;
        uint32_t level;
        uint32_t sort;
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

    // [NPCs] ini\cq_npc.csv define quais NPCs existem em cada mapa (posicao/type/lookface).
    // A aparencia e resolvida via lookfaceBase (lookface sem o ultimo digito de direcao):
    // < 1000  -> ini\npc.ini [NpcTypeNNN] -> SimpleObjID -> ini\3DSimpleObj.ini [ObjIDTypeYYYY]
    //            (partes de mesh/textura fixas, sem "roupa"), com StandByMotion via 3dmotion.ini.
    // >= 1000 -> ini\NpcX.ini [NNNN] monta um "boneco" completo (Look/Armor/Armet/RWeapon/LWeapon)
    //            ou, se Look nao for um tipo de boneco (1-4), reusa o pipeline de monstro.
    struct CachedNpcRender {
        bool isSimpleObj = true;
        std::vector<Resource::C3Model> partModels;
        std::vector<int> partTextureIds;
        int asb = 5, adb = 6;
        bool isMonsterStyle = false; // usa GetMonsterRender (Look nao e boneco)
        uint32_t monsterMeshId = 0;

        // [Hover Rest/Blaze] Apenas para NPCs simples (lookfaceBase < 1000), que tem as 3
        // motions definidas em npc.ini. Mesmos meshes/texturas de partModels, mas com a
        // motion de Rest/Blaze aplicada em vez da StandBy.
        std::vector<Resource::C3Model> partModelsRest;
        std::vector<Resource::C3Model> partModelsBlaze;
    };
    std::unordered_map<uint32_t, CachedNpcRender> m_npcRenderCache; // chave = lookfaceBase

    std::vector<Resource::NpcDbEntry> m_npcDbAll;
    std::unordered_map<uint32_t, Resource::NpcTypeConfig> m_npcTypeConfigs;
    std::unordered_map<uint32_t, Resource::SimpleObjConfig> m_simpleObjConfigs;
    std::unordered_map<uint32_t, Resource::NpcXConfig> m_npcXConfigs;
    std::vector<Game::NpcEntity> m_npcs;

    // [Gerador de Monstros] ini\cq_generator.csv define areas de spawn por mapa.
    // So fica ativo enquanto o boneco estiver no mapa correspondente (m_generators
    // e filtrado por currentMapId ao entrar no mapa, igual NPCs/regioes).
    struct ActiveGenerator : public Resource::GeneratorEntry {
        int aliveCount = 0;      // quantos monstros vivos deste gerador existem agora em m_monsters
        float restTimer = 0.0f;  // tempo decorrido desde a ultima reposicao (so conta quando abaixo do maxNpc)
    };
    std::vector<ActiveGenerator> m_generators;
    std::vector<Resource::GeneratorEntry> m_generatorDbAll;

    std::unordered_map<int16_t, int> m_puzzleTextures;
    std::unordered_map<std::string, int> m_terrainTextureCache;
    Resource::DMapData m_currentDMap;
    Resource::PulData m_currentPul;
    int m_tileSize = 256;
    float m_cameraX = 0.0f, m_cameraY = 0.0f;
    float m_zoom = 1.0f;
    int m_mouseX = 0, m_mouseY = 0;

    // [ROTACAO/INCLINACAO DO BONECO - AJUSTE AQUI] Pitch (inclinacao vertical) aplicado ao
    // desenhar o modelo do jogador em DrawMesh3D (parametro "pitch", em radianos). O boneco
    // olhando "para cima" e controlado por este valor: aumente para inclinar mais pra baixo,
    // diminua (ou use negativo) para inclinar mais pra cima. Mude so este numero e recompile
    // para testar - todos os DrawMesh3D do player usam essa constante.
    float m_playerPitch = 0.0f; // <-- MUDE ESTE VALOR PARA TESTAR (ex: 0.1f, 0.15f, -0.1f...)

    // [RANGE DE VISUALIZACAO - AJUSTE AQUI] Distancia maxima (em celulas do mapa, X+Y)
    // a partir do boneco para renderizar monstros e NPCs. Evita desenhar/atualizar
    // entidades muito longe (ex.: geradores com muitos bichos de uma vez), melhorando
    // performance e poluicao visual. Mude so este numero para testar outros valores.
    float m_entityViewRange = 30.0f; // <-- MUDE ESTE VALOR PARA TESTAR (ex: 15.0f, 30.0f...)


    // [Colisao/Agua] Acesso as celulas do mapa carregado.
    // access == 1 (Inaccessible) bloqueia movimento; surface == 1 identifica agua (nao bloqueante, apenas altera animacao).
    const Resource::MapCell* GetMapCellAt(int x, int y) const {
        if (!m_currentDMap.isValid) return nullptr;
        if (x < 0 || y < 0 || (uint32_t)x >= m_currentDMap.width || (uint32_t)y >= m_currentDMap.height) return nullptr;
        return &m_currentDMap.cells[(size_t)y * m_currentDMap.width + (size_t)x];
    }

    bool IsCellBlocked(float mapX, float mapY) const {
        const Resource::MapCell* cell = GetMapCellAt((int)std::floor(mapX), (int)std::floor(mapY));
        if (!cell) return true; // fora do mapa carregado = bloqueado
        if (cell->access == 1) return true;
        if (IsNpcOccupying(mapX, mapY)) return true; // [NPC] celula ocupada por NPC = intransitavel
        return false;
    }

    // [NPC bloqueia movimento] O NPC ocupa a celula onde esta parado, como um objeto do mapa.
    // Usado por IsCellBlocked (que por sua vez e usado tanto para andar quanto para pular),
    // portanto o jogador nao consegue nem pular nem caminhar por cima do NPC; o
    // FindWalkableTarget tambem para automaticamente antes de chegar nele (corre ate onde pode).
    bool IsNpcOccupying(float mapX, float mapY) const {
        int cx = (int)std::floor(mapX);
        int cy = (int)std::floor(mapY);
        for (const auto& npc : m_npcs) {
            if ((int)std::floor(npc.mapX) == cx && (int)std::floor(npc.mapY) == cy) return true;
        }
        return false;
    }

    bool IsCellWater(float mapX, float mapY) const {
        const Resource::MapCell* cell = GetMapCellAt((int)std::floor(mapX), (int)std::floor(mapY));
        if (!cell) return false;
        return cell->surface == 1;
    }

    // [Andar ate onde for possivel] Ao clicar num destino bloqueado (agua, obstaculo, etc.),
    // caminha em linha reta na direcao do clique e retorna o ultimo ponto valido antes do
    // bloqueio, permitindo o "melhor trajeto" ate a borda em vez de simplesmente ignorar o clique.
    // Usado apenas para andar (nao para pular, que exige o destino exato ser valido).
    void FindWalkableTarget(float fromX, float fromY, float toX, float toY, float& outX, float& outY) const {
        outX = fromX; outY = fromY;

        float dx = toX - fromX;
        float dy = toY - fromY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.001f) return;

        int steps = (std::max)(1, (int)std::ceil(dist / 0.1f));
        float stepX = dx / steps;
        float stepY = dy / steps;

        float lastValidX = fromX, lastValidY = fromY;
        for (int i = 1; i <= steps; i++) {
            float px = fromX + stepX * i;
            float py = fromY + stepY * i;
            if (IsCellBlocked(px, py)) break;
            lastValidX = px; lastValidY = py;
        }

        outX = lastValidX; outY = lastValidY;
    }

    // [Sons ambiente do mapa] ini\MusicRegion.ini define regioes retangulares (em tiles)
    // que possuem uma musica de "entrada" (TitleMusic) tocada uma vez ao entrar na regiao,
    // seguida por uma playlist de MusicN faixas que se alternam com DelayTime segundos de
    // SILENCIO entre uma faixa e outra (nao interrompe a faixa atual antes do seu tempo
    // MusicTimeN terminar), em loop enquanto o player permanecer dentro da regiao.
    void UpdateMapMusic(float deltaTime) {
        if (m_musicRegions.empty()) return;

        int px = (int)std::floor(m_player.mapX);
        int py = (int)std::floor(m_player.mapY);

        int foundRegion = -1;
        for (size_t i = 0; i < m_musicRegions.size(); i++) {
            if (m_musicRegions[i].Contains(px, py)) { foundRegion = (int)i; break; }
        }

        if (foundRegion != m_activeMusicRegion) {
            m_activeMusicRegion = foundRegion;
            m_activeMusicIndex = -1;
            m_musicTrackTimer = 0.0f;
            m_musicDelayTimer = 0.0f;
            m_musicDelayPending = false;

            if (foundRegion == -1) {
                m_audio.StopMusic();
                return;
            }

            const auto& region = m_musicRegions[foundRegion];
            if (!region.titleMusic.empty()) {
                m_audio.PlayMusic(m_clientPath + "\\" + region.titleMusic, false);
                m_musicTrackTimer = (float)region.titleMusicTime;
            }
            else {
                m_musicTrackTimer = 0.0f; // dispara a primeira faixa da playlist imediatamente
            }
            return;
        }

        if (m_activeMusicRegion == -1) return;

        const auto& region = m_musicRegions[m_activeMusicRegion];
        if (region.amount <= 0 || region.musics.empty()) return;

        // Fase de silencio (DelayTime) entre uma faixa e a proxima: a faixa anterior ja
        // tocou seu tempo (MusicTimeN) completo antes de chegarmos aqui, entao so aguarda
        // o delay e so ENTAO inicia a proxima faixa (nao corta nada no meio).
        if (m_musicDelayPending) {
            m_musicDelayTimer -= deltaTime;
            if (m_musicDelayTimer <= 0.0f) {
                m_musicDelayPending = false;

                m_activeMusicIndex++;
                if (m_activeMusicIndex >= (int)region.musics.size() || m_activeMusicIndex >= region.amount) {
                    m_activeMusicIndex = 0;
                }

                const std::string& track = region.musics[m_activeMusicIndex];
                int trackTime = (m_activeMusicIndex < (int)region.musicTimes.size()) ? region.musicTimes[m_activeMusicIndex] : 60;

                if (!track.empty()) {
                    m_audio.PlayMusic(m_clientPath + "\\" + track, false);
                }
                m_musicTrackTimer = (float)trackTime;
            }
            return;
        }

        // Faixa atual ainda tocando: respeita o tempo dela (MusicTimeN ou TitleMusicTime)
        // integralmente antes de avancar ou entrar no delay.
        m_musicTrackTimer -= deltaTime;
        if (m_musicTrackTimer <= 0.0f) {
            if (region.delayTime > 0) {
                // Entra em silencio por DelayTime segundos; a proxima faixa so comeca
                // quando esse tempo expirar (ver bloco m_musicDelayPending acima).
                m_musicDelayPending = true;
                m_musicDelayTimer = (float)region.delayTime;
            }
            else {
                m_activeMusicIndex++;
                if (m_activeMusicIndex >= (int)region.musics.size() || m_activeMusicIndex >= region.amount) {
                    m_activeMusicIndex = 0;
                }

                const std::string& track = region.musics[m_activeMusicIndex];
                int trackTime = (m_activeMusicIndex < (int)region.musicTimes.size()) ? region.musicTimes[m_activeMusicIndex] : 60;

                if (!track.empty()) {
                    m_audio.PlayMusic(m_clientPath + "\\" + track, false);
                }
                m_musicTrackTimer = (float)trackTime;
            }
        }
    }

    int m_texMainDialog1 = -1;
    int m_texMainDialog2 = -1;

    int m_texDialogTalk1 = -1;
    int m_texDialogTalk2 = -1;
    int m_texDialogTalk3 = -1;
    int m_texDialogTalk4 = -1;

    int m_texRunChk1 = -1;
    int m_texRunChk2 = -1;
    int m_texNpcEquip1 = -1;
    int m_texNpcEquip2 = -1;
    int m_texScreenMove1 = -1;
    int m_texScreenMove2 = -1;
    int m_texMapChk1 = -1;
    int m_texMapChk2 = -1;

    uint32_t m_selectedSkillId = 0;
    uint32_t m_selectedSkillLevel = 0;
    int m_attackSequence = 0;

    // Magias Mid-Air Queue e Hitbox
    bool m_pendingFacingAngle = false;
    float m_queuedFacingAngle = 0.0f;
    uint32_t m_activeCastSkillId = 0;
    uint32_t m_activeCastSort = 0;
    float m_activeCastStartX = 0.0f;
    float m_activeCastStartY = 0.0f;
    float m_activeCastTargetX = 0.0f;
    float m_activeCastTargetY = 0.0f;

    int m_texMainImgMagic = -1;
    int m_texQuerySkillBtnU = -1;
    int m_texQuerySkillBtnD = -1;
    bool m_showSkillList = false;
    int m_skillCurrentPage = 0;

    struct UISkill {
        uint32_t id;
        std::string name;
        uint32_t level;
        int tooltipTexId = -1;
        int tooltipW = 0;
        int tooltipH = 0;
    };
    std::vector<UISkill> m_uiSkills;
    std::unordered_map<uint32_t, int> m_skillIconTexIds;

    bool m_isRunning = false;
    bool m_isNpcEquipMode = false;
    bool m_isScreenMove = false;
    bool m_showMiniMap = true;

    float m_cameraOffsetX = 0.0f;
    float m_cameraOffsetY = 0.0f;
    std::vector<int> m_miniMapParts;
    int m_heroTgaId = -1;

    HCURSOR m_cursorNormal = nullptr;
    HCURSOR m_cursorNpc = nullptr;

    int m_texHpRed = -1;
    int m_texHpBlack = -1;
    int m_texHpOrange = -1;
    int m_texMpBlue = -1;
    int m_texProgressHP = -1;
    int m_texProgressMP = -1;

    int m_frameCount = 0, m_currentFps = 0;
    float m_fpsTimer = 0.0f;
    int m_debugTexId = -1;
    int m_debugTexW = 0, m_debugTexH = 0;
    std::wstring m_lastDebugStr = L"";

    // Cálculo exato de Colisão em Linha Reta para Magias Direcionais!
    float PointToSegmentDistance(float px, float py, float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        if (dx == 0.0f && dy == 0.0f) {
            dx = px - x1; dy = py - y1;
            return std::sqrt(dx * dx + dy * dy);
        }
        float t = ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy);
        t = (std::max)(0.0f, (std::min)(1.0f, t));
        float closestX = x1 + t * dx;
        float closestY = y1 + t * dy;
        dx = px - closestX;
        dy = py - closestY;
        return std::sqrt(dx * dx + dy * dy);
    }

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
                try { def.sort = std::stoul(tokens[1]); }
                catch (...) { def.sort = 0; }
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

    void LoadMagicIcons() {
        auto data = m_resource.GetFileData("ani\\Magic.Ani");
        if (data.empty()) return;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;
        uint32_t currentId = 0;

        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (line[0] == '[') {
                std::string sec = line.substr(1, line.length() - 2);
                if (sec.find("MagicSkillType") != std::string::npos) {
                    try { currentId = std::stoul(sec.substr(14)); }
                    catch (...) { currentId = 0; }
                }
                else { currentId = 0; }
            }
            else if (currentId != 0 && line.find("Frame0=") != std::string::npos) {
                std::string path = line.substr(7);
                if (!path.empty() && path.back() == '\r') path.pop_back();
                std::replace(path.begin(), path.end(), '/', '\\');

                auto imgData = m_resource.GetFileData(path);
                if (!imgData.empty()) {
                    int tex = m_renderer.LoadTextureFromMemory(imgData.data(), imgData.size());
                    if (tex != -1) m_skillIconTexIds[currentId] = tex;
                }
            }
        }

        m_uiSkills.clear();
        for (const auto& mag : m_magicDB) {
            if (m_skillIconTexIds.count(mag.id)) {
                UISkill uiSkill;
                uiSkill.id = mag.id;
                uiSkill.name = mag.name;
                uiSkill.level = mag.level;

                std::string tooltip = mag.name + " (Lv " + std::to_string(mag.level) + ")";
                std::wstring wTooltip(tooltip.begin(), tooltip.end());
                auto texData = Game::GenerateTextTexture(m_renderer, wTooltip, RGB(255, 215, 0));

                uiSkill.tooltipTexId = std::get<0>(texData);
                uiSkill.tooltipW = std::get<1>(texData);
                uiSkill.tooltipH = std::get<2>(texData);

                m_uiSkills.push_back(uiSkill);
            }
        }

        std::sort(m_uiSkills.begin(), m_uiSkills.end(), [](const UISkill& a, const UISkill& b) {
            if (a.id == b.id) return a.level < b.level;
            return a.id < b.id;
            });
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

        // [ini\Monster.txt] Le BornAction/BornEffect/BornSound por secao ([NomeDoMonstro])
        // e mescla no MonsterDef correspondente (mesmo "name" usado em dbmonster.txt).
        auto monsterTxtData = m_resource.GetFileData("ini\\Monster.txt");
        if (!monsterTxtData.empty()) {
            std::string mContent((char*)monsterTxtData.data(), monsterTxtData.size());
            std::istringstream miss(mContent);
            std::string mline;

            std::unordered_map<std::string, MonsterDef*> byName;
            for (auto& def : m_monsterDB) byName[def.name] = &def;

            MonsterDef* current = nullptr;
            auto safeStou = [](const std::string& s) -> uint32_t { try { return (uint32_t)std::stoul(s); } catch (...) { return 100; } };

            while (std::getline(miss, mline)) {
                if (!mline.empty() && mline.back() == '\r') mline.pop_back();
                if (mline.empty()) continue;

                if (mline.front() == '[' && mline.back() == ']') {
                    std::string secName = mline.substr(1, mline.size() - 2);
                    auto found = byName.find(secName);
                    current = (found != byName.end()) ? found->second : nullptr;
                    continue;
                }

                if (!current) continue;

                size_t eqPos = mline.find('=');
                if (eqPos == std::string::npos) continue;
                std::string key = mline.substr(0, eqPos);
                std::string val = mline.substr(eqPos + 1);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();

                if (key == "BornAction") current->bornAction = safeStou(val);
                else if (key == "BornEffect") current->bornEffect = val;
                else if (key == "BornSound") current->bornSound = val;
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

    void LoadMiniMap(int mapId) {
        m_miniMapParts.clear();
        auto data = m_resource.GetFileData("ani\\MiniMap.Ani");
        if (data.empty()) return;

        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;
        bool inSection = false;
        std::string targetSection = "[" + std::to_string(mapId) + "]";

        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (line[0] == '[') {
                if (line.find(targetSection) != std::string::npos) inSection = true;
                else inSection = false;
            }
            else if (inSection) {
                if (line.find("Frame") != std::string::npos && line.find("=") != std::string::npos && line.find("FrameAmount") == std::string::npos) {
                    std::string path = line.substr(line.find("=") + 1);
                    if (!path.empty() && path.back() == '\r') path.pop_back();

                    auto imgData = m_resource.GetFileData(path);
                    if (!imgData.empty()) {
                        int tex = m_renderer.LoadTextureFromMemory(imgData.data(), imgData.size());
                        if (tex != -1) m_miniMapParts.push_back(tex);
                    }
                }
            }
        }
    }

    bool Initialize(HINSTANCE hInstance) {
        // [Config.ini] Carrega ClientPath, resolucao, fullscreen, vsync e fps limit antes de criar
        // a janela/dispositivo, para que a janela ja nasca com o tamanho/modo configurado.
        LoadConfigIni("config.ini");

        m_window.m_width = m_configWidth;
        m_window.m_height = m_configHeight;
        if (!m_window.Create(hInstance, L"Conquer Kayank - Engine Master", m_configFullscreen)) return false;
        m_renderer.Initialize(m_window.m_hWnd, m_window.m_width, m_window.m_height);
        m_renderer.SetVSync(m_configVSync);

        // [TESTE] Inicializa o modulo Graphics_Riot com o mesmo device/context do renderer
        // principal e carrega um unico campeao para validar o pipeline .skn/.skl/.anm.
        // Os campos abaixo tambem alimentam o painel ImGui "Riot Champion (LoL)", permitindo
        // trocar o modelo em tempo real sem reiniciar o jogo.
        GraphicsRiot::Initialize(m_renderer.GetD3DDevice(), m_renderer.GetD3DContext(), m_window.m_width, m_window.m_height);
        //m_riotSknPath = "D:\\coisas lol\\2026_extraidos\\champs\\assets\\characters\\aurelionsol\\skins\\base\\aurelionsol.skn";
        //m_riotSklPath = "D:\\coisas lol\\2026_extraidos\\champs\\assets\\characters\\aurelionsol\\skins\\base\\aurelionsol.skl";
        //m_riotAnmPath = "D:\\coisas lol\\2026_extraidos\\champs\\assets\\characters\\aurelionsol\\skins\\base\\animations\\aurelionsol_idle1.anm";
        //m_riotTexPath = L"D:\\coisas lol\\2026_extraidos\\champs\\assets\\characters\\aurelionsol\\skins\\base\\aurelionsol_body_tx_cm.ds";
        ApplyRiotChampion();

        LoadRiotChampionsIni("lol_personagens.ini");


        m_audio.Initialize();

        m_window.onMouseWheel = [this](int delta) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse) return;

            if (delta > 0) m_zoom += 0.1f;
            else if (delta < 0) m_zoom -= 0.1f;
            if (m_zoom < 0.5f) m_zoom = 0.5f;
            if (m_zoom > 3.0f) m_zoom = 3.0f;
            };

        // [Log de carregamento - WDF] Marca a duracao da leitura dos pacotes .wdf do cliente.
        auto wdfLoadStart = std::chrono::steady_clock::now();
        if (!m_resource.Initialize(m_clientPath)) return false;
        double wdfLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wdfLoadStart).count();
        std::cout << "[Load] wdf demorou " << wdfLoadSeconds << " segundos para carregar...\n";

        // [Tela de carregamento] Sorteia data\main\LogoN.bmp
        // pulando os que nao existem) e exibe centralizado 500x375 enquanto o restante dos
        // recursos (mapas, npcs, monstros, efeitos, etc.) e carregado logo abaixo. A tela
        // fecha sozinha (DeleteTexture) assim que o carregamento termina, antes do jogo abrir.
        {
            srand((unsigned int)time(NULL));
            std::vector<int> candidates = { 1, 2, 3, 4, 5 };
            for (size_t i = candidates.size() - 1; i > 0; i--) {
                size_t j = rand() % (i + 1);
                std::swap(candidates[i], candidates[j]);
            }

            for (int n : candidates) {
                char logoPath[64];
                sprintf_s(logoPath, "data\\main\\Logo%d.bmp", n);
                auto logoData = m_resource.GetFileData(logoPath);
                if (!logoData.empty()) {
                    m_splashTexId = m_renderer.LoadTextureFromMemory(logoData.data(), logoData.size());
                    if (m_splashTexId != -1) break;
                }
            }

            if (m_splashTexId != -1) {
                MSG splashMsg = {};
                while (PeekMessage(&splashMsg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&splashMsg); DispatchMessage(&splashMsg);
                }

                int splashX = (m_window.m_width - 500) / 2;
                int splashY = (m_window.m_height - 375) / 2;
                m_renderer.BeginFrame();
                m_renderer.DrawSprite(m_splashTexId, splashX, splashY, 500, 375);
                m_renderer.EndFrame();
            }
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

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

        m_npcTypeConfigs = m_resource.ParseNpcTypeIni("ini\\npc.ini");
        m_simpleObjConfigs = m_resource.ParseSimpleObjIni("ini\\3DSimpleObj.ini");
        m_npcXConfigs = m_resource.ParseNpcXIni("ini\\NpcX.ini");
        m_npcDbAll = m_resource.ParseNpcCsv("ini\\cq_npc.csv");
        m_generatorDbAll = m_resource.ParseGeneratorCsv("ini\\cq_generator.csv");

        LoadItemTypes("ini\\itemtype.txt");
        LoadMonsterDB();
        LoadMagicDB();

        auto loadGuiFile = [&](const std::string& path) -> int {
            auto data = m_resource.GetFileData(path);
            if (!data.empty()) return m_renderer.LoadTextureFromMemory(data.data(), data.size());
            return -1;
            };

        // [Log de carregamento - DDS's] Marca a duracao do carregamento das texturas .dds da UI principal.
        auto ddsLoadStart = std::chrono::steady_clock::now();

        m_texMainDialog1 = loadGuiFile("data\\main\\mainDialog1.dds");
        m_texMainDialog2 = loadGuiFile("data\\main\\mainDialog2.dds");

        m_texDialogTalk1 = loadGuiFile("data\\main\\DialogTalk1.dds");
        m_texDialogTalk2 = loadGuiFile("data\\main\\DialogTalk2.dds");
        m_texDialogTalk3 = loadGuiFile("data\\main\\DialogTalk3.dds");
        m_texDialogTalk4 = loadGuiFile("data\\main\\DialogTalk4.dds");

        m_texRunChk1 = loadGuiFile("data\\main\\RunChk1.dds");
        m_texRunChk2 = loadGuiFile("data\\main\\RunChk2.dds");
        m_texNpcEquip1 = loadGuiFile("data\\main\\NpcEquip.dds");
        m_texNpcEquip2 = loadGuiFile("data\\main\\NpcEquipClick.dds");
        m_texScreenMove1 = loadGuiFile("data\\main\\ScreenMoveChk1.dds");
        m_texScreenMove2 = loadGuiFile("data\\main\\ScreenMoveChk2.dds");
        m_texMapChk1 = loadGuiFile("data\\main\\MapChk1.dds");
        m_texMapChk2 = loadGuiFile("data\\main\\MapChk2.dds");

        m_texMainImgMagic = loadGuiFile("data\\main\\MainImgMagic.dds");
        m_texQuerySkillBtnU = loadGuiFile("data\\main\\QuerySkillBtnU.dds");
        m_texQuerySkillBtnD = loadGuiFile("data\\main\\QuerySkillBtnD.dds");

        m_heroTgaId = loadGuiFile("data\\minimap\\hero0.tga");
        m_texProgressHP = loadGuiFile("data\\main\\ProgressHP.dds");
        m_texProgressMP = loadGuiFile("data\\main\\ProgressMP.dds");

        double ddsLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - ddsLoadStart).count();
        std::cout << "[Load] dds's demoraram " << ddsLoadSeconds << " segundos para carregar...\n";

        LoadMagicIcons();

        m_texHpRed = Game::GenerateColorDDS(m_renderer, 220, 20, 20);
        m_texHpBlack = Game::GenerateColorDDS(m_renderer, 15, 15, 15);
        m_texHpOrange = Game::GenerateColorDDS(m_renderer, 255, 140, 0);
        m_texMpBlue = Game::GenerateColorDDS(m_renderer, 50, 150, 255);
        m_texHpBlueRiot = Game::GenerateColorDDS(m_renderer, 40, 120, 255);

        auto texData = Game::GenerateTextTexture(m_renderer, L"KayanK", RGB(255, 255, 255));
        m_player.nameTexId = std::get<0>(texData);
        m_player.nameW = std::get<1>(texData);
        m_player.nameH = std::get<2>(texData);

        std::string cursorNormalPath = m_clientPath + "\\data\\Cursor\\Normal.ani";
        std::string cursorNpcPath = m_clientPath + "\\data\\Cursor\\Descyr.ico";
        m_cursorNormal = LoadCursorFromFileA(cursorNormalPath.c_str());
        m_cursorNpc = LoadCursorFromFileA(cursorNpcPath.c_str());

        if (m_cursorNormal) {
            SetClassLongPtr(m_window.m_hWnd, GCLP_HCURSOR, (LONG_PTR)m_cursorNormal);
            SetCursor(m_cursorNormal); ShowCursor(TRUE);
        }

        m_player.armorId = 0;
        m_currentArmetId = 0;
        m_currentGarmentId = 0;

        // [Log de carregamento - Mesh do boneco] Marca a duracao do carregamento dos modelos
        // .c3 (arma, armadura, elmo, roupa) do personagem principal.
        auto meshLoadStart = std::chrono::steady_clock::now();

        ChangeWeapon(0, 0);
        ChangeArmor(Game::ModelType::SmallMale, m_player.armorId);
        ChangeArmet(Game::ModelType::SmallMale, m_currentArmetId);
        ChangeGarment(Game::ModelType::SmallMale, m_currentGarmentId);

        double meshLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - meshLoadStart).count();
        std::cout << "[Load] mesh do boneco demorou " << meshLoadSeconds << " segundos para carregar...\n";

        auto gameMaps = m_resource.LoadGameMapDat("ini\\GameMap.dat");
        const int currentMapId = 1005; // TwinCity: possui arvores, agua e elevacao de terreno (escadas)

        auto allMusicRegions = m_resource.ParseMusicRegions("ini\\MusicRegion.ini");
        m_musicRegions.clear();
        for (auto& region : allMusicRegions) {
            if (region.mapId == (uint32_t)currentMapId) m_musicRegions.push_back(region);
        }
        m_activeMusicRegion = -1;
        m_activeMusicIndex = -1;
        m_musicTrackTimer = 0.0f;
        m_musicDelayTimer = 0.0f;
        m_musicDelayPending = false;

        auto allMapRegions = m_resource.ParseRegions("ini\\region.ini");
        m_mapRegions.clear();
        for (auto& region : allMapRegions) {
            if (region.mapId == (uint32_t)currentMapId) m_mapRegions.push_back(region);
        }
        m_activeRegionNameIdx = -1;
        m_activeRegionEffectIdx = -1;

        // [NPCs] Filtra cq_npc.csv pelo mapa atual e resolve nome/posicao (aparencia
        // e resolvida sob demanda em UpdateMap/render via GetNpcRender + cache).
        for (auto& npc : m_npcs) {
            if (npc.nameTexId != -1) m_renderer.DeleteTexture(npc.nameTexId);
        }
        m_npcs.clear();
        for (auto& dbEntry : m_npcDbAll) {
            if (dbEntry.mapId != (uint32_t)currentMapId) continue;

            Game::NpcEntity npc;
            npc.dbId = dbEntry.id;
            npc.name = dbEntry.name;
            npc.type = dbEntry.type;
            npc.mapX = (float)dbEntry.cellX + 0.5f;
            npc.mapY = (float)dbEntry.cellY + 0.5f;

            uint32_t lookfaceBase = dbEntry.lookface / 10;
            uint32_t direction = dbEntry.lookface % 10;

            // [ROTACAO DO NPC - AJUSTE AQUI] Cada uma das 8 direcoes do lookface gira 45 graus
            // (0.78539816f rad = PI/4). O NPC_FACING_OFFSET abaixo e um deslocamento extra
            // somado ao angulo final; mude apenas este valor (em radianos) para girar o boneco
            // suavemente para cima/baixo/lado e ir testando ate achar o ideal.
            // Referencia rapida: 0.0f = sem deslocamento extra; valores positivos giram num
            // sentido, negativos no outro. 0.78539816f equivale a 45 graus, 0.39269908f a 22.5 graus, etc.
            constexpr float NPC_FACING_OFFSET = 0.78539816f; // <-- MUDE ESTE VALOR PARA TESTAR
            npc.facingAngle = -((float)direction * 0.78539816f) - NPC_FACING_OFFSET;
            npc.isSimpleObj = lookfaceBase < 1000;
            npc.cacheIndex = (int)lookfaceBase;

            std::wstring wideName(npc.name.begin(), npc.name.end());
            auto texData = Game::GenerateTextTexture(m_renderer, wideName, RGB(255, 255, 0));
            npc.nameTexId = std::get<0>(texData);
            npc.nameW = std::get<1>(texData);
            npc.nameH = std::get<2>(texData);

            m_npcs.push_back(npc);
        }

        // [Gerador de Monstros] So ativa geradores do mapa atual: ao trocar de mapa os
        // spawns do mapa anterior param (m_generators e limpo) e os monstros ja existentes
        // sao removidos, pois cada gerador so deve existir enquanto o boneco estiver no mapa dele.
        m_generators.clear();
        for (auto& genDef : m_generatorDbAll) {
            if (genDef.mapId != (uint32_t)currentMapId) continue;
            ActiveGenerator gen;
            static_cast<Resource::GeneratorEntry&>(gen) = genDef;
            m_generators.push_back(gen);
        }
        for (auto& mob : m_monsters) {
            if (mob.nameTexId != -1) m_renderer.DeleteTexture(mob.nameTexId);
        }
        m_monsters.clear();

        // [Log de carregamento - Mapa] Marca a duracao do carregamento do mapa (.dmap, .pul,
        // minimapa, tiles, terrain objects, scene objects/pontes e portais). Nao inclui
        // efeitos, monstros ou NPCs, que sao tratados separadamente.
        auto mapLoadStart = std::chrono::steady_clock::now();

        if (gameMaps.count(currentMapId)) {
            m_currentDMap = m_resource.LoadDMap(gameMaps[currentMapId].dmapPath);
            if (m_currentDMap.isValid) {
                m_player.mapX = m_currentDMap.width / 2.0f;
                m_player.mapY = m_currentDMap.height / 2.0f;

                m_currentPul = m_resource.LoadPul(m_currentDMap.puzzleFile);
                m_tileSize = gameMaps[currentMapId].tileSize > 0 ? gameMaps[currentMapId].tileSize : 256;

                LoadMiniMap(currentMapId);

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

                // [Perf] terrainObjects frequentemente repetem o mesmo par aniPath/aniName (ex: mesma
                // arvore/decoracao usada dezenas de vezes num mapa grande como o 1002). Cacheamos a
                // textura e as dimensoes em pixel por essa chave para evitar reler/redecodificar e
                // recriar a mesma textura na GPU a cada ocorrencia.
                std::unordered_map<std::string, std::pair<int, int>> terrainSizeCache;
                for (const auto& terrain : m_currentDMap.terrainObjects) {
                    std::string key = terrain.aniPath + "|" + terrain.aniName;
                    auto cachedTex = m_terrainTextureCache.find(key);

                    int textureId = -1;
                    int pixelW = 0, pixelH = 0;
                    if (cachedTex != m_terrainTextureCache.end()) {
                        textureId = cachedTex->second;
                        auto sizeIt = terrainSizeCache.find(key);
                        if (sizeIt != terrainSizeCache.end()) { pixelW = sizeIt->second.first; pixelH = sizeIt->second.second; }
                    }
                    else {
                        std::string ddsPath = m_resource.ParseAniSection(terrain.aniPath, terrain.aniName);
                        if (!ddsPath.empty()) {
                            auto terrainTexData = m_resource.GetFileData(ddsPath);
                            if (!terrainTexData.empty()) {
                                textureId = m_renderer.LoadTextureFromMemory(terrainTexData.data(), terrainTexData.size());
                                Resource::ReadImagePixelSize(terrainTexData, pixelW, pixelH);
                                m_terrainTextureCache[key] = textureId;
                                terrainSizeCache[key] = { pixelW, pixelH };
                            }
                        }
                    }

                    if (textureId != -1) {
                        Game::SceneObject obj;
                        obj.textureId = textureId;

                        // [Fix] terrain.width/height do .dmap representam a area em TILES (footprint no chao),
                        // nao o tamanho em pixels da textura. Usar esses valores direto no DrawSprite fazia
                        // arvores/objetos renderizarem como sprites minusculos (poucos pixels) e praticamente invisiveis.
                        if (pixelW > 0 && pixelH > 0) {
                            obj.width = pixelW; obj.height = pixelH;
                        }
                        else {
                            obj.width = terrain.width; obj.height = terrain.height;
                        }

                        obj.mapX = (float)terrain.mapX; obj.mapY = (float)terrain.mapY;
                        obj.offsetX = terrain.offsetX; obj.offsetY = terrain.offsetY;
                        obj.depthKey = obj.mapX + obj.mapY;
                        m_sceneObjects.push_back(obj);
                    }
                }

                // [MAP_SCENE / pontes] Cada entrada de m_currentDMap.sceneObjects aponta (por
                // caminho) para um arquivo de cena separado (ex.: ponte) que contem varias
                // "partes" (sprites animados) posicionadas em celulas relativas a posicao da
                // cena. Carregamos esse arquivo e expandimos cada parte como um SceneObject
                // normal, reaproveitando o mesmo pipeline de renderizacao dos terrainObjects.
                std::cout << "[SceneObj] mapa tem " << m_currentDMap.sceneObjects.size() << " sceneObjects (pontes/etc.)\n";
                for (const auto& sceneObj : m_currentDMap.sceneObjects) {
                    Resource::SceneFileData sceneFile = m_resource.LoadScene(sceneObj.scenePath);
                    std::cout << "[SceneObj] scenePath='" << sceneObj.scenePath << "' pos=(" << sceneObj.mapX << "," << sceneObj.mapY
                        << ") isValid=" << (sceneFile.isValid ? 1 : 0) << " parts=" << sceneFile.parts.size() << "\n";
                    if (!sceneFile.isValid) continue;

                    for (const auto& part : sceneFile.parts) {
                        std::string key = part.aniPath + "|" + part.aniName;
                        auto cachedTex = m_terrainTextureCache.find(key);

                        int textureId = -1;
                        int pixelW = 0, pixelH = 0;
                        if (cachedTex != m_terrainTextureCache.end()) {
                            textureId = cachedTex->second;
                            auto sizeIt = terrainSizeCache.find(key);
                            if (sizeIt != terrainSizeCache.end()) { pixelW = sizeIt->second.first; pixelH = sizeIt->second.second; }
                        }
                        else {
                            std::string ddsPath = m_resource.ParseAniSection(part.aniPath, part.aniName);
                            if (!ddsPath.empty()) {
                                auto partTexData = m_resource.GetFileData(ddsPath);
                                if (!partTexData.empty()) {
                                    textureId = m_renderer.LoadTextureFromMemory(partTexData.data(), partTexData.size());
                                    Resource::ReadImagePixelSize(partTexData, pixelW, pixelH);
                                    m_terrainTextureCache[key] = textureId;
                                    terrainSizeCache[key] = { pixelW, pixelH };
                                }
                                else {
                                    std::cout << "[SceneObj] FALHA ao ler dados do arquivo ddsPath='" << ddsPath
                                        << "' (aniPath='" << part.aniPath << "' aniName='" << part.aniName << "')\n";
                                }
                            }
                            else {
                                std::cout << "[SceneObj] FALHA ao resolver ddsPath a partir de aniPath='" << part.aniPath
                                    << "' aniName='" << part.aniName << "'\n";
                            }
                        }

                        if (textureId != -1) {
                            Game::SceneObject obj;
                            obj.textureId = textureId;
                            obj.width = pixelW; obj.height = pixelH;
                            obj.mapX = (float)(sceneObj.mapX + part.locationX);
                            obj.mapY = (float)(sceneObj.mapY + part.locationY);
                            // [Fix] O pipeline de desenho compartilhado (node.type==2) SUBTRAI obj.offsetX/Y da posicao
                            // de tela (correto para terrainObjects, conforme TerrainObjectDrawingComponent da referencia).
                            // Porem partes de cena/ponte (SceneDrawingComponent da referencia) SOMAM o ImageOffset em vez
                            // de subtrair. Negamos aqui para reaproveitar o mesmo pipeline com o sinal correto.
                            obj.offsetX = -part.imageOffsetX; obj.offsetY = -part.imageOffsetY;

                            // [Depth/z-order] O restante do pipeline (jogador, monstros, NPCs) ordena
                            // exclusivamente pela celula-ancora (mapX+mapY), igual ao TerrainObjectDrawingComponent
                            // de referencia ("sort by cell depth: Location.X + Location.Y"). Usar o canto mais
                            // distante do retangulo da parte fazia a ponte quase sempre "ganhar" da comparacao de
                            // profundidade contra o boneco, mesmo quando ele estava numa celula da tela mais
                            // proxima da camera do que a ancora da ponte - por isso ela sempre desenhava por cima.
                            // Mantemos a mesma convencao usada pelas demais entidades: apenas a celula-ancora.
                            obj.depthKey = obj.mapX + obj.mapY;

                            m_sceneObjects.push_back(obj);

                            // [Colisao/pulo] O grid de celulas do proprio arquivo de cena (part.cells) descreve
                            // apenas o sprite retangular da parte da ponte; como a ponte e "diagonal" dentro
                            // desse retangulo, varias celulas nos cantos ficam marcadas como Inaccessible(1)
                            // mesmo fazendo parte do caminho visual da ponte. Copiar esse grid cru por
                            // cima do mapa (como antes) deixava buracos que bloqueavam andar/pular.
                            //
                            // A referencia (MapFileLoader.TrySetAccess) marca apenas a celula-ancora da cena
                            // como Accessible/Scene; para o footprint inteiro da parte ficar andavel/pulavel,
                            // forcamos TODAS as celulas do retangulo da parte como acessiveis (access=0) e
                            // fora d'agua (surface=0), em vez de usar o access bruto do arquivo de cena.
                            if (part.sizeW > 0 && part.sizeH > 0) {
                                for (int cy = 0; cy < part.sizeH; cy++) {
                                    for (int cx = 0; cx < part.sizeW; cx++) {
                                        int mapCellX = sceneObj.mapX + part.locationX + cx;
                                        int mapCellY = sceneObj.mapY + part.locationY + cy;
                                        if (mapCellX < 0 || mapCellY < 0 ||
                                            (uint32_t)mapCellX >= m_currentDMap.width || (uint32_t)mapCellY >= m_currentDMap.height) continue;
                                        Resource::MapCell& mapCell = m_currentDMap.cells[(size_t)mapCellY * m_currentDMap.width + mapCellX];
                                        mapCell.access = 0;  // Accessible
                                        mapCell.surface = 0; // nao-agua (nao muda animacao de nadar sobre a ponte)
                                    }
                                }
                            }
                        }
                    }
                }

                // [Portais] Cada entrada de m_currentDMap.portals marca uma celula de teletransporte
                // (.dmap). O efeito visual "exit" (3DEffect.ini) e disparado fixo na posicao do
                // mundo de cada portal e ja possui LoopTime muito alto (praticamente continuo).
                for (const auto& portal : m_currentDMap.portals) {
                    LoadEffect("Exit", (float)portal.mapX + 0.5f, (float)portal.mapY + 0.5f, 0.0f, 0.0f, false, -1, 0.0f, false);
                }
            }
        }

        double mapLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - mapLoadStart).count();
        std::cout << "[Load] mapa demorou " << mapLoadSeconds << " segundos para carregar...\n";

        // [Tela de carregamento] Tudo carregado (mapa, npcs, geradores, efeitos, etc.):
        // fecha a splash antes de abrir a tela normal do jogo.
        if (m_splashTexId != -1) {
            m_renderer.DeleteTexture(m_splashTexId);
            m_splashTexId = -1;
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

    // [NPCs] Resolve e cacheia a aparencia visual de um NPC a partir do lookfaceBase
    // (lookface do cq_npc.csv sem o ultimo digito de direcao).
    CachedNpcRender& GetNpcRender(uint32_t lookfaceBase) {
        auto cacheIt = m_npcRenderCache.find(lookfaceBase);
        if (cacheIt != m_npcRenderCache.end()) return cacheIt->second;

        CachedNpcRender render;

        if (lookfaceBase < 1000) {
            // npc.ini [NpcTypeNNN] -> 3DSimpleObj.ini [ObjIDTypeYYYY]
            render.isSimpleObj = true;
            auto typeIt = m_npcTypeConfigs.find(lookfaceBase);
            if (typeIt != m_npcTypeConfigs.end()) {
                auto& typeCfg = typeIt->second;
                render.asb = typeCfg.asb;
                render.adb = typeCfg.adb;

                auto objIt = m_simpleObjConfigs.find(typeCfg.simpleObjId);
                if (objIt != m_simpleObjConfigs.end()) {
                    for (auto& part : objIt->second.parts) {
                        Resource::C3Model model;
                        int texId = -1;

                        if (m_c3Paths.count(part.mesh)) {
                            model = m_resource.LoadC3Model(m_c3Paths[part.mesh]);
                        }
                        if (m_ddsPaths.count(part.texture)) {
                            auto texData = m_resource.GetFileData(m_ddsPaths[part.texture]);
                            if (!texData.empty()) texId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                        }

                        // Animacao StandBy do NPC (o modelo fica parado exibindo essa motion).
                        Resource::C3Model standModel = model;
                        if (m_motionPaths.count(typeCfg.standByMotion)) {
                            Resource::C3Model animModel = m_resource.LoadC3Model(m_motionPaths[typeCfg.standByMotion]);
                            ApplyAnim(standModel, animModel);
                        }
                        render.partModels.push_back(standModel);
                        render.partTextureIds.push_back(texId);

                        // [Hover] Rest (toca uma vez) e Blaze (loop) para quando o mouse passar por cima.
                        Resource::C3Model restModel = model;
                        if (m_motionPaths.count(typeCfg.restMotion)) {
                            Resource::C3Model animModel = m_resource.LoadC3Model(m_motionPaths[typeCfg.restMotion]);
                            ApplyAnim(restModel, animModel);
                        }
                        render.partModelsRest.push_back(restModel);

                        Resource::C3Model blazeModel = model;
                        if (m_motionPaths.count(typeCfg.blazeMotion)) {
                            Resource::C3Model animModel = m_resource.LoadC3Model(m_motionPaths[typeCfg.blazeMotion]);
                            ApplyAnim(blazeModel, animModel);
                        }
                        render.partModelsBlaze.push_back(blazeModel);
                    }
                }
            }
        }
        else {
            // NpcX.ini [NNNN]: se Look for um tipo de boneco valido (1-4), monta como
            // personagem completo; caso contrario, reusa o pipeline de monstro (mesh direto).
            auto xIt = m_npcXConfigs.find(lookfaceBase);
            if (xIt != m_npcXConfigs.end()) {
                auto& xCfg = xIt->second;

                if (xCfg.look >= 1 && xCfg.look <= 4) {
                    render.isSimpleObj = false;
                    render.isMonsterStyle = false;

                    uint32_t modelPrefix = xCfg.look;
                    Resource::C3Model animIdle = GetActionModel((Game::ModelType)modelPrefix, xCfg.rWeapon, xCfg.lWeapon, Game::RoleActionType::StandBy);

                    uint32_t baseArmorId = (modelPrefix * 1000000) + xCfg.armor;
                    if (m_armorConfigs.count(baseArmorId)) {
                        auto& armCfg = m_armorConfigs[baseArmorId];
                        for (auto& pCfg : armCfg.parts) {
                            Resource::C3Model model;
                            int texId = -1;
                            if (m_c3Paths.count(pCfg.mesh)) {
                                model = m_resource.LoadC3Model(m_c3Paths[pCfg.mesh]);
                                ApplyAnim(model, animIdle);
                            }
                            if (m_ddsPaths.count(pCfg.texture)) {
                                auto texData = m_resource.GetFileData(m_ddsPaths[pCfg.texture]);
                                if (!texData.empty()) texId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                            }
                            render.asb = pCfg.asb; render.adb = pCfg.adb;
                            render.partModels.push_back(model);
                            render.partTextureIds.push_back(texId);
                        }
                    }

                    if (xCfg.armet != 0) {
                        uint32_t baseArmetId = (modelPrefix * 1000000) + xCfg.armet;
                        if (m_armetConfigs.count(baseArmetId)) {
                            auto& armCfg = m_armetConfigs[baseArmetId];
                            for (auto& pCfg : armCfg.parts) {
                                Resource::C3Model model;
                                int texId = -1;
                                if (m_c3Paths.count(pCfg.mesh)) {
                                    model = m_resource.LoadC3Model(m_c3Paths[pCfg.mesh]);
                                    ApplyAnim(model, animIdle);
                                }
                                if (m_ddsPaths.count(pCfg.texture)) {
                                    auto texData = m_resource.GetFileData(m_ddsPaths[pCfg.texture]);
                                    if (!texData.empty()) texId = m_renderer.LoadTextureFromMemory(texData.data(), texData.size());
                                }
                                render.partModels.push_back(model);
                                render.partTextureIds.push_back(texId);
                            }
                        }
                    }
                }
                else {
                    // Look nao e um tipo de boneco (ex.: monstro/animal) -> mesh direto via GetMonsterRender.
                    render.isMonsterStyle = true;
                    render.monsterMeshId = xCfg.look;
                }
            }
        }

        m_npcRenderCache[lookfaceBase] = render;
        return m_npcRenderCache[lookfaceBase];
    }

    void LoadEffect(const std::string& effectName, float mapX = -1.0f, float mapY = -1.0f, float screenOffsetX = 0.0f, float screenOffsetY = 0.0f, bool isDamage = false, int overrideDelay = -1, float angle = 0.0f, bool isScreenFixed = false) {
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
        newActiveEffect.angle = angle;

        newActiveEffect.baseOffsetY = screenOffsetY;
        newActiveEffect.isDamageNumber = isDamage;
        newActiveEffect.currentLife = 0.0f;
        newActiveEffect.maxLife = 2.0f;
        newActiveEffect.isScreenFixed = isScreenFixed;

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

    // [Nome de regiao + efeito 3D] ini\region.ini: enquanto o player estiver dentro do
    // range x,y -> x+cx,y+cy de uma entrada, exibe seu regionName (texto discreto sobre
    // o minimap) e mantem o effectName correspondente tocando via LoadEffect/3DEffect.ini.
    void UpdateMapRegions(float deltaTime) {
        if (m_mapRegions.empty()) return;

        int px = (int)std::floor(m_player.mapX);
        int py = (int)std::floor(m_player.mapY);

        int foundNameIdx = -1;
        int foundEffectIdx = -1;
        for (size_t i = 0; i < m_mapRegions.size(); i++) {
            const auto& region = m_mapRegions[i];
            if (!region.Contains(px, py)) continue;
            if (foundNameIdx == -1 && !region.regionName.empty()) foundNameIdx = (int)i;
            if (foundEffectIdx == -1 && !region.effectName.empty()) foundEffectIdx = (int)i;
        }

        if (foundNameIdx != m_activeRegionNameIdx) {
            m_activeRegionNameIdx = foundNameIdx;
            if (m_regionNameTexId != -1) { m_renderer.DeleteTexture(m_regionNameTexId); m_regionNameTexId = -1; }

            if (foundNameIdx != -1) {
                const std::string& name = m_mapRegions[foundNameIdx].regionName;
                std::wstring wName(name.begin(), name.end());
                auto texData = Game::GenerateTextTexture(m_renderer, wName, RGB(255, 255, 255));
                m_regionNameTexId = std::get<0>(texData);
                m_regionNameW = std::get<1>(texData);
                m_regionNameH = std::get<2>(texData);
            }
        }

        if (foundEffectIdx != m_activeRegionEffectIdx) {
            m_activeRegionEffectIdx = foundEffectIdx;
            if (foundEffectIdx != -1) {
                // Sem mapX/mapY: o efeito e desenhado direto no centro da tela (isScreenFixed
                // ignora o jumpZ do personagem, diferente do efeito de agua que acompanha o pulo).
                // Dispara uma unica vez ao entrar na regiao; so volta a aparecer se sair do
                // range e entrar novamente (nao repete enquanto o player permanecer dentro).
                LoadEffect(m_mapRegions[foundEffectIdx].effectName, -1.0f, -1.0f, 0.0f, 0.0f, false, -1, 0.0f, true);
            }
        }
    }


    void LoadTME(const std::string& tmeFile, float startX, float startY, float angle) {
        if (tmeFile.empty() || tmeFile == "NULL") return;

        Resource::TMEData tmeParsed = m_resource.ParseTME("ini\\tme\\" + tmeFile);
        if (!tmeParsed.isValid) return;

        // [CORREÇÃO MATEMÁTICA] Linha reta baseada no MapAngle perfeitamente projetada!
        float mapAngle = -angle + 0.785398f;

        for (const auto& node : tmeParsed.nodes) {
            if (node.effectName.empty()) continue;

            float distTiles = node.distance / 40.0f;

            float targetX = startX + std::cos(mapAngle) * distTiles;
            float targetY = startY + std::sin(mapAngle) * distTiles;

            LoadEffect(node.effectName, targetX, targetY, 0.0f, 0.0f, false, node.delay, angle);
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

        Resource::C3Model animIdle = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::StandBy);
        Resource::C3Model animWalkL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)110);
        Resource::C3Model animWalkR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)111);
        Resource::C3Model animRunL = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)120);
        Resource::C3Model animRunR = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, (Game::RoleActionType)121);
        Resource::C3Model animJump = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Jump);
        Resource::C3Model animAlert = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Alert);
        if (!animAlert.isValid || animAlert.motions.empty()) animAlert = animIdle;
        Resource::C3Model animSwim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Swim);
        if (!animSwim.isValid || animSwim.motions.empty()) animSwim = animIdle;

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
                    newPart.runModel = baseModel; ApplyWalkAnim(newPart.runModel, animRunL, animRunR);
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);
                    newPart.swimModel = baseModel; ApplyAnim(newPart.swimModel, animSwim);

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
        Resource::C3Model animSwim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Swim);
        if (!animSwim.isValid || animSwim.motions.empty()) animSwim = animIdle;

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
                    newPart.runModel = baseModel; ApplyWalkAnim(newPart.runModel, animRunL, animRunR);
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);
                    newPart.swimModel = baseModel; ApplyAnim(newPart.swimModel, animSwim);

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
        Resource::C3Model animSwim = GetActionModel(m_player.modelType, m_player.rightHandWeaponId, m_player.leftHandWeaponId, Game::RoleActionType::Swim);
        if (!animSwim.isValid || animSwim.motions.empty()) animSwim = animIdle;

        ApplyAnim(m_hairIdleModel, animIdle);
        ApplyAnim(m_hairJumpModel, animJump);
        ApplyAnim(m_hairAlertModel, animAlert);
        ApplyAnim(m_hairSwimModel, animSwim);
        ApplyWalkAnim(m_hairWalkModel, animWalkL, animWalkR);
        ApplyWalkAnim(m_hairRunModel, animRunL, animRunR);

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
                    newPart.runModel = baseModel;  ApplyWalkAnim(newPart.runModel, animRunL, animRunR);
                    newPart.jumpModel = baseModel; ApplyAnim(newPart.jumpModel, animJump);
                    newPart.alertModel = baseModel; ApplyAnim(newPart.alertModel, animAlert);
                    newPart.swimModel = baseModel; ApplyAnim(newPart.swimModel, animSwim);

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

    void ApplyRiotChampion() {
        if (GraphicsRiot::IsRiotModelValid(m_riotTestHandle)) {
            GraphicsRiot::UnloadRiotChampion(m_riotTestHandle);
            m_riotTestHandle = GraphicsRiot::InvalidRiotModelHandle;
        }

        if (m_riotSknPath.empty() || m_riotSklPath.empty()) {
            std::cout << "[Riot] Nao foi possivel aplicar: informe ao menos o .skn e o .skl." << std::endl;
            return;
        }

        m_riotTestHandle = GraphicsRiot::LoadRiotChampion(
            m_riotSknPath.c_str(),
            m_riotSklPath.c_str(),
            m_riotAnmPath.c_str(),
            m_riotTexPath.c_str());

        if (!GraphicsRiot::IsRiotModelValid(m_riotTestHandle)) {
            std::cout << "[Riot] Falha ao aplicar o champion informado via ImGui." << std::endl;
        }
        else {
            std::cout << "[Riot] Champion aplicado com sucesso via ImGui." << std::endl;
        }
    }

    void RemoveRiotChampion() {
        if (GraphicsRiot::IsRiotModelValid(m_riotTestHandle)) {
            GraphicsRiot::UnloadRiotChampion(m_riotTestHandle);
        }
        m_riotTestHandle = GraphicsRiot::InvalidRiotModelHandle;
    }

    void UpdateRiotNameTexture() {
        if (m_riotNameTexId != -1) {
            m_renderer.DeleteTexture(m_riotNameTexId);
            m_riotNameTexId = -1;
        }
        if (m_riotName.empty()) return;

        std::wstring wname(m_riotName.begin(), m_riotName.end());
        // Ciano, conforme pedido (nome do champion Riot em destaque acima da cabeca).
        auto texData = Game::GenerateTextTexture(m_renderer, wname, RGB(0, 255, 255));
        m_riotNameTexId = std::get<0>(texData);
        m_riotNameW = std::get<1>(texData);
        m_riotNameH = std::get<2>(texData);
    }

    void DrawImGuiPanel() {
        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("Painel de Controle KayanK");

        if (ImGui::CollapsingHeader("Riot Champion (LoL)")) {
            auto pathField = [this](const char* label, const wchar_t* dialogTitleFilter, std::string& path) {
                char buf[512];
                strncpy_s(buf, path.c_str(), _TRUNCATE);
                ImGui::PushID(label);
                ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("...")) {
                    std::wstring wpath(path.begin(), path.end());
                    if (OpenFileDialog(m_window.m_hWnd, dialogTitleFilter, wpath)) {
                        path.assign(wpath.begin(), wpath.end());
                    }
                }
                ImGui::PopID();
                };

            pathField("Arquivo .skn", L"Arquivos SKN (*.skn)\0*.skn\0Todos os arquivos (*.*)\0*.*\0", m_riotSknPath);
            pathField("Arquivo .skl", L"Arquivos SKL (*.skl)\0*.skl\0Todos os arquivos (*.*)\0*.*\0", m_riotSklPath);
            pathField("Arquivo .anm", L"Arquivos ANM (*.anm)\0*.anm\0Todos os arquivos (*.*)\0*.*\0", m_riotAnmPath);

            {
                std::string texPathUtf8(m_riotTexPath.begin(), m_riotTexPath.end());
                char buf[512];
                strncpy_s(buf, texPathUtf8.c_str(), _TRUNCATE);
                ImGui::InputText("Textura (.dds)", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("...##tex")) {
                    if (OpenFileDialog(m_window.m_hWnd, L"Texturas Riot (*.dds;*.tex)\0*.dds;*.tex\0Arquivos DDS (*.dds)\0*.dds\0Arquivos TEX (*.tex)\0*.tex\0Todos os arquivos (*.*)\0*.*\0", m_riotTexPath)) {
                        // m_riotTexPath ja foi atualizado pelo dialogo
                    }
                }
            }

            ImGui::SliderFloat("Angulo (graus)", &m_riotAngleDeg, 0.0f, 360.0f);
            ImGui::SliderFloat("Escala", &m_riotScale, 0.1f, 5.0f);

            {
                char nameBuf[128];
                strncpy_s(nameBuf, m_riotName.c_str(), _TRUNCATE);
                if (ImGui::InputText("Nome do Personagem", nameBuf, sizeof(nameBuf))) {
                    m_riotName = nameBuf;
                    UpdateRiotNameTexture();
                }
            }
            ImGui::SliderFloat("Vida (HP %)", &m_riotHpRatio, 0.0f, 1.0f);

            if (ImGui::Button("Aplicar", ImVec2(150.0f, 0.0f))) {
                ApplyRiotChampion();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remover", ImVec2(150.0f, 0.0f))) {
                RemoveRiotChampion();
            }

            ImGui::Text("Status: %s", GraphicsRiot::IsRiotModelValid(m_riotTestHandle) ? "Ativo" : "Removido/Nao carregado");
        }

        if (ImGui::CollapsingHeader("Riot Champion 2 (LoL)")) {
            ImGui::TextWrapped("Catalogo: lol_personagens.ini | Raiz dos assets: %s", m_riotAssetsRoot.c_str());

            if (ImGui::Button("Recarregar catalogo (.ini)")) {
                m_riot2ChampList.clear();
                m_riot2SkinsByChamp.clear();
                LoadRiotChampionsIni("lol_personagens.ini");
                m_riot2ChampIdx = 0;
                m_riot2SkinIdx = 0;
                m_riot2AnimIdx = 0;
                ResolveRiot2Selection();
            }

            if (m_riot2ChampList.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Nenhum campeao encontrado no catalogo.");
            }
            else {
                bool selectionChanged = false;

                if (ImGui::BeginCombo("Campeao", m_riot2ChampList[m_riot2ChampIdx].c_str())) {
                    for (int n = 0; n < (int)m_riot2ChampList.size(); n++) {
                        bool isSel = (m_riot2ChampIdx == n);
                        if (ImGui::Selectable(m_riot2ChampList[n].c_str(), isSel)) {
                            if (m_riot2ChampIdx != n) { m_riot2ChampIdx = n; m_riot2SkinIdx = 0; selectionChanged = true; }
                        }
                        if (isSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const std::string& currentChamp = m_riot2ChampList[m_riot2ChampIdx];
                auto skinsIt = m_riot2SkinsByChamp.find(currentChamp);
                if (skinsIt != m_riot2SkinsByChamp.end() && !skinsIt->second.empty()) {
                    if (m_riot2SkinIdx < 0 || m_riot2SkinIdx >= (int)skinsIt->second.size()) m_riot2SkinIdx = 0;
                    if (ImGui::BeginCombo("Skin", skinsIt->second[m_riot2SkinIdx].c_str())) {
                        for (int n = 0; n < (int)skinsIt->second.size(); n++) {
                            bool isSel = (m_riot2SkinIdx == n);
                            if (ImGui::Selectable(skinsIt->second[n].c_str(), isSel)) {
                                if (m_riot2SkinIdx != n) { m_riot2SkinIdx = n; selectionChanged = true; }
                            }
                            if (isSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                if (selectionChanged) {
                    m_riot2AnimIdx = 0;
                    ResolveRiot2Selection();
                }

                ResolveRiot2Selection();

                if (m_riot2ResolvedAnims.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Nenhuma animacao encontrada para esta selecao.");
                }
                else {
                    if (m_riot2AnimIdx < 0 || m_riot2AnimIdx >= (int)m_riot2ResolvedAnims.size()) m_riot2AnimIdx = 0;
                    if (ImGui::BeginCombo("Animacao", m_riot2ResolvedAnims[m_riot2AnimIdx].name.c_str())) {
                        for (int n = 0; n < (int)m_riot2ResolvedAnims.size(); n++) {
                            bool isSel = (m_riot2AnimIdx == n);
                            if (ImGui::Selectable(m_riot2ResolvedAnims[n].name.c_str(), isSel)) {
                                if (m_riot2AnimIdx != n) {
                                    m_riot2AnimIdx = n;
                                    if (m_riot2Enabled) ApplyRiot2Champion();
                                }
                            }
                            if (isSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::Text("skn: %s", m_riot2ResolvedSkn.empty() ? "(nao encontrado)" : m_riot2ResolvedSkn.c_str());
                ImGui::Text("skl: %s", m_riot2ResolvedSkl.empty() ? "(nao encontrado)" : m_riot2ResolvedSkl.c_str());
                ImGui::Text("texturas encontradas: %d", (int)m_riot2ResolvedTexPaths.size());
            }

            ImGui::SliderFloat("Angulo (graus)##r2", &m_riot2AngleDeg, 0.0f, 360.0f);
            ImGui::SliderFloat("Escala##r2", &m_riot2Scale, 0.1f, 5.0f);

            {
                char nameBuf2[128];
                strncpy_s(nameBuf2, m_riot2Name.c_str(), _TRUNCATE);
                if (ImGui::InputText("Nome do Personagem##r2", nameBuf2, sizeof(nameBuf2))) {
                    m_riot2Name = nameBuf2;
                    UpdateRiot2NameTexture();
                }
            }
            ImGui::SliderFloat("Vida (HP %)##r2", &m_riot2HpRatio, 0.0f, 1.0f);

            bool enabledToggle = m_riot2Enabled;
            if (ImGui::Checkbox("Ativar Champion 2", &enabledToggle)) {
                m_riot2Enabled = enabledToggle;
                if (m_riot2Enabled) ApplyRiot2Champion();
                else RemoveRiot2Champion();
            }

            ImGui::Text("Status: %s", GraphicsRiot::IsRiotModelValid(m_riot2Handle) ? "Ativo" : "Removido/Nao carregado");

            // Lista de meshes (objetos) do modelo carregado, igual ao viewer de referencia em
            // Rust: cada submesh (corpo, arma, yoyo, etc.) pode ser mostrado/ocultado e ter sua
            // textura trocada por qualquer .tex/.dds encontrado na pasta da skin.
            if (GraphicsRiot::IsRiotModelValid(m_riot2Handle)) {
                int subMeshCount = GraphicsRiot::GetRiotSubMeshCount(m_riot2Handle);
                if (subMeshCount > 0) {
                    if ((int)m_riot2SubMeshTexChoice.size() != subMeshCount) m_riot2SubMeshTexChoice.resize(subMeshCount, -1);

                    if (ImGui::CollapsingHeader("Meshes##r2", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (int i = 0; i < subMeshCount; ++i) {
                            ImGui::PushID(i);
                            const char* meshName = GraphicsRiot::GetRiotSubMeshName(m_riot2Handle, i);
                            bool visible = GraphicsRiot::IsRiotSubMeshVisible(m_riot2Handle, i);
                            if (ImGui::Checkbox(meshName && meshName[0] ? meshName : "(sem nome)", &visible)) {
                                GraphicsRiot::SetRiotSubMeshVisible(m_riot2Handle, i, visible);
                            }

                            int& texChoice = m_riot2SubMeshTexChoice[i];
                            std::string currentLabel = (texChoice >= 0 && texChoice < (int)m_riot2ResolvedTexPaths.size())
                                ? m_riot2ResolvedTexPaths[texChoice] : std::string("(padrao)");
                            if (ImGui::BeginCombo("Textura", currentLabel.c_str())) {
                                bool isDefaultSel = (texChoice < 0);
                                if (ImGui::Selectable("(padrao)", isDefaultSel)) {
                                    texChoice = -1;
                                    GraphicsRiot::SetRiotSubMeshTexture(m_riot2Handle, i, L"");
                                }
                                if (isDefaultSel) ImGui::SetItemDefaultFocus();

                                for (int t = 0; t < (int)m_riot2ResolvedTexPaths.size(); ++t) {
                                    bool isSel = (texChoice == t);
                                    if (ImGui::Selectable(m_riot2ResolvedTexPaths[t].c_str(), isSel)) {
                                        texChoice = t;
                                        std::string fullUtf8 = m_riotAssetsRoot + m_riot2ResolvedTexPaths[t];
                                        std::wstring full(fullUtf8.begin(), fullUtf8.end());
                                        GraphicsRiot::SetRiotSubMeshTexture(m_riot2Handle, i, full.c_str());
                                    }
                                    if (isSel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::PopID();
                        }
                    }
                }
            }
        }


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
                    newMob.uid = m_nextMonsterUid++;

                    newMob.mapX = m_player.mapX + (float)((rand() % 5) - 2);
                    newMob.mapY = m_player.mapY + (float)((rand() % 5) - 2);
                    newMob.startX = newMob.mapX;
                    newMob.startY = newMob.mapY;

                    const auto& mDef = m_monsterDB[monster_idx];
                    newMob.meshId = mDef.meshId;
                    newMob.maxHp = mDef.maxLife;
                    newMob.hp = newMob.maxHp;
                    newMob.visualHp = (float)newMob.maxHp;
                    newMob.currentAction = (int)mDef.bornAction;
                    newMob.hasPlayedBornAction = false;

                    std::wstring wideName(mDef.name.begin(), mDef.name.end());
                    auto texData = Game::GenerateTextTexture(m_renderer, wideName, RGB(255, 255, 255));
                    newMob.nameTexId = std::get<0>(texData);
                    newMob.nameW = std::get<1>(texData);
                    newMob.nameH = std::get<2>(texData);

                    m_monsters.push_back(newMob);

                    if (!mDef.bornEffect.empty() && mDef.bornEffect != "none" && mDef.bornEffect != "None") {
                        size_t beforeCount = m_activeEffects.size();
                        LoadEffect(mDef.bornEffect, newMob.mapX, newMob.mapY);
                        if (m_activeEffects.size() > beforeCount) {
                            m_activeEffects.back().attachedMonsterUid = newMob.uid;
                        }
                    }
                    if (!mDef.bornSound.empty() && mDef.bornSound != "none" && mDef.bornSound != "None") {
                        m_audio.PlaySoundEffect(m_clientPath + "\\" + mDef.bornSound);
                    }
                }
            }
            else {
                ImGui::Text("Banco de Dados dbmonster.txt nao carregado.");
            }
        }

        if (ImGui::CollapsingHeader("Efeitos (Invocar)")) {
            static std::vector<std::string> allEffectNames;
            if (allEffectNames.empty()) {
                for (auto& pair : m_effectConfigs) allEffectNames.push_back(pair.first);
                std::sort(allEffectNames.begin(), allEffectNames.end());
            }

            static char effectFilter[128] = "";
            ImGui::InputText("Filtrar por nome", effectFilter, IM_ARRAYSIZE(effectFilter));

            static int effect_idx = 0;
            static std::vector<std::string> filteredEffectNames;
            filteredEffectNames.clear();
            std::string filterLower = effectFilter;
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
            for (auto& name : allEffectNames) {
                std::string nameLower = name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                if (filterLower.empty() || nameLower.find(filterLower) != std::string::npos) {
                    filteredEffectNames.push_back(name);
                }
            }
            if (effect_idx >= (int)filteredEffectNames.size()) effect_idx = 0;

            std::string effectPreview = filteredEffectNames.empty() ? "Nenhum efeito encontrado" : filteredEffectNames[effect_idx];
            if (ImGui::BeginCombo("Lista de Efeitos", effectPreview.c_str())) {
                for (int n = 0; n < (int)filteredEffectNames.size(); n++) {
                    const bool is_selected = (effect_idx == n);
                    if (ImGui::Selectable(filteredEffectNames[n].c_str(), is_selected)) {
                        effect_idx = n;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Invocar Efeito ao Lado do Personagem", ImVec2(-1.0f, 40.0f))) {
                if (!filteredEffectNames.empty()) {
                    LoadEffect(filteredEffectNames[effect_idx], m_player.mapX + 1.0f, m_player.mapY);
                }
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
            GraphicsRiot::Resize(currentWidth, currentHeight);
        }

        if (GraphicsRiot::IsRiotModelValid(m_riotTestHandle)) {
            GraphicsRiot::UpdateRiotAnimation(m_riotTestHandle, deltaTime);
        }
        if (GraphicsRiot::IsRiotModelValid(m_riot2Handle)) {
            GraphicsRiot::UpdateRiotAnimation(m_riot2Handle, deltaTime);
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

        static bool s_prevRightDown = false;
        bool currentRightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        bool rightClicked = currentRightDown && !s_prevRightDown;
        s_prevRightDown = currentRightDown;

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

        bool mouseOnUI = false;
        int totalW = 1024;
        int startX = (m_window.m_width - totalW) / 2;
        int screenBottom = m_window.m_height;

        int bootX = startX + 1;    int bootY = screenBottom - 118;
        int equipX = startX + 22;  int equipY = screenBottom - 130;
        int moveX = startX + 50;   int moveY = screenBottom - 130;
        int mapX = startX + 72;    int mapY = screenBottom - 118;

        if (pt.x >= bootX && pt.x <= bootX + 32 && pt.y >= bootY && pt.y <= bootY + 32) {
            mouseOnUI = true;
            if (leftClicked) {
                m_isRunning = !m_isRunning;
                leftClicked = false;
            }
        }

        if (pt.x >= equipX && pt.x <= equipX + 32 && pt.y >= equipY && pt.y <= equipY + 32) {
            mouseOnUI = true;
            if (leftClicked) {
                m_isNpcEquipMode = !m_isNpcEquipMode;
                HCURSOR cur = m_isNpcEquipMode ? m_cursorNpc : m_cursorNormal;
                SetCursor(cur);
                SetClassLongPtr(m_window.m_hWnd, GCLP_HCURSOR, (LONG_PTR)cur);
                leftClicked = false;
            }
        }

        if (pt.x >= moveX && pt.x <= moveX + 32 && pt.y >= moveY && pt.y <= moveY + 32) {
            mouseOnUI = true;
            if (leftClicked) {
                m_isScreenMove = !m_isScreenMove;
                if (!m_isScreenMove) { m_cameraOffsetX = 0.0f; m_cameraOffsetY = 0.0f; }
                leftClicked = false;
            }
        }

        if (pt.x >= mapX && pt.x <= mapX + 32 && pt.y >= mapY && pt.y <= mapY + 32) {
            mouseOnUI = true;
            if (leftClicked) {
                m_showMiniMap = !m_showMiniMap;
                leftClicked = false;
            }
        }

        int magicBtnX = startX + 750;
        int magicBtnY = screenBottom - 48;

        if (pt.x >= magicBtnX && pt.x <= magicBtnX + 64 && pt.y >= magicBtnY && pt.y <= magicBtnY + 64) {
            mouseOnUI = true;
            if (leftClicked) {
                m_showSkillList = !m_showSkillList;
                leftClicked = false;
            }
        }

        if (m_showSkillList) {
            int popupStartX = magicBtnX - 45;
            int popupStartY = magicBtnY - 140;

            int upX = popupStartX + 85; int upY = popupStartY + 10;
            int downX = popupStartX + 85; int downY = popupStartY + 100;

            if (pt.x >= upX && pt.x <= upX + 16 && pt.y >= upY && pt.y <= upY + 16) {
                mouseOnUI = true;
                if (leftClicked && m_skillCurrentPage > 0) { m_skillCurrentPage--; leftClicked = false; }
            }
            if (pt.x >= downX && pt.x <= downX + 16 && pt.y >= downY && pt.y <= downY + 16) {
                mouseOnUI = true;
                int maxPages = (m_uiSkills.size() + 5) / 6;
                if (leftClicked && m_skillCurrentPage < maxPages - 1) { m_skillCurrentPage++; leftClicked = false; }
            }

            int startIndex = m_skillCurrentPage * 6;
            for (int i = 0; i < 6; i++) {
                int skillIndex = startIndex + i;
                if (skillIndex >= m_uiSkills.size()) break;

                int col = i % 2; int row = i / 2;
                int sx = popupStartX + (col * 40);
                int sy = popupStartY + (row * 40);

                if (pt.x >= sx && pt.x <= sx + 32 && pt.y >= sy && pt.y <= sy + 32) {
                    mouseOnUI = true;
                    if (leftClicked) {
                        m_selectedSkillId = m_uiSkills[skillIndex].id;
                        m_selectedSkillLevel = m_uiSkills[skillIndex].level;
                        m_showSkillList = false;
                        leftClicked = false;
                    }
                }
            }

            if (pt.x >= popupStartX - 5 && pt.x <= popupStartX + 105 && pt.y >= popupStartY - 5 && pt.y <= popupStartY + 130) mouseOnUI = true;
        }

        if (pt.x >= startX && pt.x <= startX + 1024 && pt.y >= screenBottom - 128 && pt.y <= screenBottom) {
            mouseOnUI = true;
        }

        if (m_isNpcEquipMode && (leftClicked || rightClicked) && !mouseOnUI) {
            m_isNpcEquipMode = false;
            SetCursor(m_cursorNormal);
            SetClassLongPtr(m_window.m_hWnd, GCLP_HCURSOR, (LONG_PTR)m_cursorNormal);
            leftClicked = false;
        }

        static POINT lastMousePt = pt;
        bool isAltDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        if (m_isScreenMove && isAltDown && !mouseOnUI && !m_isNpcEquipMode) {
            m_cameraOffsetX -= (pt.x - lastMousePt.x) / m_zoom;
            m_cameraOffsetY -= (pt.y - lastMousePt.y) / m_zoom;

            if (m_cameraOffsetX > 1500.0f) m_cameraOffsetX = 1500.0f;
            if (m_cameraOffsetX < -1500.0f) m_cameraOffsetX = -1500.0f;
            if (m_cameraOffsetY > 1500.0f) m_cameraOffsetY = 1500.0f;
            if (m_cameraOffsetY < -1500.0f) m_cameraOffsetY = -1500.0f;

            leftClicked = false;
        }
        lastMousePt = pt;

        if (hasFocus && isMouseInside && !io.WantCaptureMouse && !mouseOnUI && !m_isNpcEquipMode && (!m_isScreenMove || !isAltDown)) {
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

                // [Hover/Clique NPC] Detecta se o mouse ENTROU sobre um NPC (edge trigger), para disparar
                // a animacao Rest->Blaze uma unica vez. A animacao roda ate o fim mesmo que o mouse
                // saia do NPC antes de terminar; ela nao fica "presa" ao hover continuo.
                // O mesmo hit-test (caixa de tela do NPC, cobrindo a altura do modelo) tambem serve
                // para impedir que um clique "em cima" do NPC seja tratado como andar para tras dele:
                // so conta como clique de interacao (virar o NPC) se cair dentro dessa caixa; cliques
                // fora dela (mesmo que logo atras do NPC) continuam sendo andar normalmente.
                int clickedNpcIdx = -1;
                for (size_t ni = 0; ni < m_npcs.size(); ni++) {
                    auto& npc = m_npcs[ni];
                    auto [nWorldX, nWorldY] = coordSystem.MapToScreen(npc.mapX, npc.mapY);
                    float drawX = nWorldX - m_cameraX;
                    float drawY = nWorldY - m_cameraY;
                    float zX = cx + (drawX - cx) * m_zoom;
                    float zY = cy + (drawY - cy) * m_zoom;

                    float npcW = 80.0f * m_zoom;
                    float npcH = 130.0f * m_zoom;
                    float npcLeft = zX - (npcW / 2.0f);
                    float npcRight = zX + (npcW / 2.0f);
                    float npcTop = zY - npcH;
                    float npcBottom = zY + (20.0f * m_zoom);

                    bool nowHovered = (pt.x >= npcLeft && pt.x <= npcRight && pt.y >= npcTop && pt.y <= npcBottom);
                    if (nowHovered && !npc.isHovered && npc.animState == 0) {
                        // Comecou o hover (e o NPC estava parado): toca RestMotion uma vez,
                        // depois BlazeMotion uma vez, e so entao volta a StandByMotion sozinho.
                        npc.animState = 1;
                        npc.currentFrame = 0;
                        npc.animTimer = 0.0f;
                    }
                    npc.isHovered = nowHovered;

                    if (nowHovered) clickedNpcIdx = (int)ni;

                    // [Clique no NPC] Gira o NPC para encarar o jogador.
                    if (leftClicked && nowHovered) {
                        float ndx = m_player.mapX - npc.mapX;
                        float ndy = m_player.mapY - npc.mapY;
                        if (std::sqrt(ndx * ndx + ndy * ndy) > 0.05f) {
                            npc.facingAngle = -(std::atan2(ndy, ndx) - 0.78539f);
                        }
                    }
                }

                if (leftClicked && clickedMonsterIdx != -1) {
                    m_player.targetMonsterIndex = clickedMonsterIdx;
                    m_player.isMoving = false;
                    m_player.hasQueuedAction = false;
                    m_player.isChasing = true;

                    m_player.isAlert = false;
                    m_player.alertTimer = 0.0f;
                }
                else if (leftClicked && clickedMonsterIdx == -1 && clickedNpcIdx == -1) {
                    m_player.targetMonsterIndex = -1;
                    m_player.isAttacking = false;
                    m_player.isChasing = false;

                    float dx = targetX - m_player.mapX;
                    float dy = targetY - m_player.mapY;
                    float dist = std::sqrt(dx * dx + dy * dy);

                    bool isShiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

                    if (isShiftDown) {
                        // [Shift+clique] Apenas vira o boneco na direcao do clique, sem andar nem pular.
                        if (!m_player.isJumping && dist > 0.05f) {
                            m_player.facingAngle = -(std::atan2(dy, dx) - 0.70539f);
                        }
                    }
                    else if (!m_player.isJumping) {
                        if (dist > 0.05f) m_player.facingAngle = -(std::atan2(dy, dx) - 0.70539f);

                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                            if (!IsCellBlocked(targetX, targetY)) {
                                m_player.isJumping = true; m_player.isMoving = false;
                                m_player.jumpTimer = 0.0f; m_player.startMapX = m_player.mapX; m_player.startMapY = m_player.mapY;
                                m_player.targetMapX = targetX; m_player.targetMapY = targetY; m_player.currentFrame = 0;

                                PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), 130);
                            }
                        }
                        else {
                            if (!IsCellBlocked(targetX, targetY)) {
                                m_player.targetMapX = targetX; m_player.targetMapY = targetY; m_player.isMoving = true;
                            }
                            else {
                                // Destino bloqueado (agua, obstaculo, escada em area intransitavel, etc.):
                                // caminha em linha reta ate o ultimo ponto valido antes do bloqueio,
                                // em vez de simplesmente ignorar o clique.
                                float walkX, walkY;
                                FindWalkableTarget(m_player.mapX, m_player.mapY, targetX, targetY, walkX, walkY);
                                float walkDist = std::sqrt((walkX - m_player.mapX) * (walkX - m_player.mapX) + (walkY - m_player.mapY) * (walkY - m_player.mapY));
                                if (walkDist > 0.05f) {
                                    m_player.targetMapX = walkX; m_player.targetMapY = walkY; m_player.isMoving = true;
                                }
                            }
                        }
                    }
                    else {
                        m_player.hasQueuedAction = true;
                        m_player.queuedTargetX = targetX;
                        m_player.queuedTargetY = targetY;
                        m_player.queuedIsJump = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
                    }
                }
                else if (rightClicked && m_selectedSkillId != 0) {
                    m_player.targetMonsterIndex = -1;
                    m_player.isChasing = false;
                    m_player.isMoving = false;

                    float castX = m_player.isJumping ? m_player.targetMapX : m_player.mapX;
                    float castY = m_player.isJumping ? m_player.targetMapY : m_player.mapY;

                    float dx = targetX - castX;
                    float dy = targetY - castY;
                    float newAngle = -(std::atan2(dy, dx) - 0.78539f);

                    if (m_player.isJumping) {
                        m_pendingFacingAngle = true;
                        m_queuedFacingAngle = newAngle;
                    }
                    else {
                        m_player.facingAngle = newAngle;
                    }

                    float mapAngle = -newAngle + 0.785398f;

                    MagicDef* magic = nullptr;
                    for (auto& m : m_magicDB) {
                        if (m.id == m_selectedSkillId && m.level == m_selectedSkillLevel) {
                            magic = &m; break;
                        }
                    }

                    if (magic) {
                        m_activeCastSkillId = magic->id;
                        m_activeCastSort = magic->sort;
                        m_activeCastStartX = castX;
                        m_activeCastStartY = castY;
                        m_activeCastTargetX = castX + std::cos(mapAngle) * 14.0f;
                        m_activeCastTargetY = castY + std::sin(mapAngle) * 14.0f;

                        uint32_t finalAction = magic->actionId;
                        if (finalAction == 401) {
                            finalAction = 401 + m_attackSequence;
                            m_attackSequence++;
                            if (m_attackSequence > 2) m_attackSequence = 0;
                        }

                        if (m_player.isJumping) {
                            m_player.hasQueuedAttack = true;
                            m_player.queuedAttackIndex = finalAction;
                            m_player.queuedAttackAnim = (Game::RoleActionType)finalAction;
                        }
                        else {
                            m_player.isMoving = false;
                            m_player.isAttacking = true;
                            m_player.currentFrame = 0;
                            m_player.currentAttackIndex = finalAction;
                            m_player.currentAttackAnim = (Game::RoleActionType)finalAction;
                            PlayActionSound((uint32_t)m_player.modelType, GetWeaponPrefix(m_player.rightHandWeaponId, m_player.leftHandWeaponId), finalAction);
                            m_player.isAlert = true;
                            m_player.alertTimer = 5.0f;
                        }

                        if (!magic->intoneEffect.empty()) LoadEffect(magic->intoneEffect, castX, castY, 0, 0, false, -1, newAngle);
                        if (!magic->senderEffect.empty()) LoadEffect(magic->senderEffect, castX, castY, 0, 0, false, -1, newAngle);
                        if (!magic->soundPath.empty()) m_audio.PlaySoundEffect(m_clientPath + "\\" + magic->soundPath);

                        if (!magic->tmeFile.empty()) LoadTME(magic->tmeFile, castX, castY, newAngle);

                        if (!magic->targetEffect.empty() && magic->tmeFile.empty()) {
                            if (magic->sort == 14) {
                                LoadEffect(magic->targetEffect, castX, castY, 0.0f, 0.0f, false, -1, newAngle);
                            }
                            else {
                                LoadEffect(magic->targetEffect, targetX, targetY, 0.0f, 0.0f, false, -1, 0.0f);
                            }
                        }
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

                if (m_pendingFacingAngle) {
                    m_player.facingAngle = m_queuedFacingAngle;
                    m_pendingFacingAngle = false;
                }

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
                    float speed = m_isRunning ? 7.0f : 3.0f;
                    float nextX = m_player.mapX + (dx / dist) * speed * deltaTime;
                    float nextY = m_player.mapY + (dy / dist) * speed * deltaTime;
                    if (!IsCellBlocked(nextX, m_player.mapY)) m_player.mapX = nextX;
                    if (!IsCellBlocked(m_player.mapX, nextY)) m_player.mapY = nextY;
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
                        m_activeCastSkillId = 0;
                        m_player.isAttacking = true;
                        m_player.currentFrame = 0;
                        m_player.currentAttackIndex = 401 + m_attackSequence;
                        m_attackSequence++;
                        if (m_attackSequence > 2) m_attackSequence = 0;

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
                float speed = m_isRunning ? 7.0f : 3.0f;
                float nextX = m_player.mapX + (dx / dist) * speed * deltaTime;
                float nextY = m_player.mapY + (dy / dist) * speed * deltaTime;
                bool movedX = false, movedY = false;
                if (!IsCellBlocked(nextX, m_player.mapY)) { m_player.mapX = nextX; movedX = true; }
                if (!IsCellBlocked(m_player.mapX, nextY)) { m_player.mapY = nextY; movedY = true; }
                if (!movedX && !movedY) {
                    m_player.isMoving = false; m_player.currentFrame = 0;
                }
            }
        }

        m_player.isInWater = IsCellWater(m_player.mapX, m_player.mapY);

        UpdateMapMusic(deltaTime);
        UpdateMapRegions(deltaTime);

        m_player.animTimer += deltaTime;
        float currentAnimSpeed = 10.0f;

        if (m_player.isJumping) currentAnimSpeed = 15.0f;
        else if (m_player.isAttacking) currentAnimSpeed = 25.0f;
        else if (m_player.isMoving) currentAnimSpeed = m_isRunning ? 20.0f : 12.0f;

        if (m_player.animTimer >= (1.0f / currentAnimSpeed)) {
            m_player.currentFrame++;
            m_player.animTimer -= (1.0f / currentAnimSpeed);

            if (m_player.isMoving) {
                if (m_player.currentFrame % 14 == 0) {
                    PlayActionSound((uint32_t)m_player.modelType, 999, m_isRunning ? 120 : 110);
                    if (m_player.isInWater) LoadEffect("WaterSplash", m_player.mapX, m_player.mapY);
                }
                else if (m_player.currentFrame % 14 == 7) {
                    PlayActionSound((uint32_t)m_player.modelType, 999, m_isRunning ? 121 : 111);
                    if (m_player.isInWater) LoadEffect("WaterSplash", m_player.mapX, m_player.mapY);
                }
            }

            if (m_player.isAttacking) {
                if (m_player.currentFrame == 10) {
                    if (m_activeCastSkillId != 0 && m_activeCastSort == 14) {
                        for (size_t i = 0; i < m_monsters.size(); i++) {
                            auto& mob = m_monsters[i];
                            if (mob.isDead) continue;

                            float dist = PointToSegmentDistance(mob.mapX, mob.mapY, m_activeCastStartX, m_activeCastStartY, m_activeCastTargetX, m_activeCastTargetY);

                            if (dist <= 1.5f) {
                                int damage = 800 + (rand() % 400);
                                mob.hp -= damage;

                                std::string dmgStr = std::to_string(damage);
                                float digitSpacing = 30.0f;
                                float startX = -((dmgStr.length() - 1) * digitSpacing) / 2.0f;

                                for (size_t k = 0; k < dmgStr.length(); ++k) {
                                    std::string effectName = "CountB" + std::string(1, dmgStr[k]);
                                    LoadEffect(effectName, mob.mapX, mob.mapY, startX + (k * digitSpacing), 130.0f, true);
                                }

                                if (mob.hp <= 0 && !mob.isDead) {
                                    mob.hp = 0; mob.isDead = true; mob.currentAction = 330; mob.currentFrame = 0;
                                    mob.animTimer = 0.0f; mob.deathTimer = 0.0f; mob.alpha = 1.0f;
                                    PlayActionSound(mob.meshId, 999, 330);
                                    if (m_player.targetMonsterIndex == i) { m_player.targetMonsterIndex = -1; m_player.isChasing = false; }
                                }
                            }
                        }
                    }
                    else if (m_player.targetMonsterIndex != -1 && m_player.targetMonsterIndex < m_monsters.size()) {
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
                    m_activeCastSkillId = 0;

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

        // [Animacao dos NPCs] Avanca o frame da motion ativa (StandBy/Rest/Blaze) para cada
        // NPC visivel no mapa atual, em loop, igual e feito com monstros/player. animState
        // controla qual motion esta tocando: 0=StandBy (loop), 1=Rest (uma vez ao passar o
        // mouse), 2=Blaze (loop enquanto o hover permanece ativo).
        for (auto& npc : m_npcs) {
            npc.animTimer += deltaTime;

            // StandBy toca devagar (idle); Rest/Blaze (disparados pelo hover) tocam na
            // velocidade normal de animacao (mesma taxa usada por monstros/player em acoes).
            float npcAnimSpeed = (npc.animState == 0) ? 8.0f : 16.0f;

            if (npc.animTimer >= (1.0f / npcAnimSpeed)) {
                npc.animTimer -= (1.0f / npcAnimSpeed);
                npc.currentFrame++;

                auto& render = GetNpcRender((uint32_t)npc.cacheIndex);
                int maxFrames = 1;
                if (render.isMonsterStyle) {
                    auto& monsterRender = GetMonsterRender(render.monsterMeshId, 100);
                    if (monsterRender.model.isValid && !monsterRender.model.motions.empty()) {
                        maxFrames = monsterRender.model.motions[0].frameCount;
                    }
                }
                else {
                    const std::vector<Resource::C3Model>* activeParts = &render.partModels;
                    if (npc.animState == 1) activeParts = &render.partModelsRest;
                    else if (npc.animState == 2) activeParts = &render.partModelsBlaze;

                    if (!activeParts->empty() && (*activeParts)[0].isValid && !(*activeParts)[0].motions.empty()) {
                        maxFrames = (*activeParts)[0].motions[0].frameCount;
                    }
                }
                if (maxFrames <= 0) maxFrames = 1;

                if (npc.currentFrame >= maxFrames) {
                    npc.currentFrame = 0;
                    if (npc.animState == 1) {
                        // RestMotion terminou (toca uma vez) -> passa para BlazeMotion (tambem uma vez).
                        npc.animState = 2;
                    }
                    else if (npc.animState == 2) {
                        // BlazeMotion terminou (toca uma vez) -> volta ao StandByMotion (idle normal).
                        npc.animState = 0;
                    }
                }
            }
        }

        std::vector<Game::MonsterEntity> spawnedMonsters;

        // [Gerador de Monstros] Recalcula quantos monstros vivos existem por gerador e,
        // se houver espaco (aliveCount < maxNpc), aguarda rest_secs e entao nasce ate
        // max_per_gen por ciclo dentro da area (bound_x,bound_y,bound_cx,bound_cy) ou no
        // ponto fixo (born_x,born_y) quando definido. So roda enquanto o mapa atual tiver
        // geradores carregados (m_generators e filtrado pelo mapa em que o boneco esta).
        if (!m_generators.empty()) {
            for (auto& gen : m_generators) gen.aliveCount = 0;
            for (auto& mob : m_monsters) {
                if (mob.isDead || mob.originGeneratorId == 0) continue;
                for (auto& gen : m_generators) {
                    if (gen.id == mob.originGeneratorId) { gen.aliveCount++; break; }
                }
            }

            for (auto& gen : m_generators) {
                if (gen.maxNpc <= 0 || gen.aliveCount >= gen.maxNpc) {
                    gen.restTimer = 0.0f;
                    continue;
                }

                // [Range de visualizacao] So nasce monstro se o boneco estiver perto da area
                // do gerador (dentro de m_entityViewRange), evitando travamentos por spawns
                // em massa em geradores longe da tela. O timer de descanso so comeca a contar
                // quando o boneco entra no alcance.
                {
                    float nearestX = (std::max)((float)gen.boundX, (std::min)(m_player.mapX, (float)(gen.boundX + gen.boundCx)));
                    float nearestY = (std::max)((float)gen.boundY, (std::min)(m_player.mapY, (float)(gen.boundY + gen.boundCy)));
                    float gdx = m_player.mapX - nearestX;
                    float gdy = m_player.mapY - nearestY;
                    if (std::sqrt(gdx * gdx + gdy * gdy) > m_entityViewRange) {
                        gen.restTimer = 0.0f;
                        continue;
                    }
                }

                gen.restTimer += deltaTime;
                if (gen.restTimer < (float)gen.restSecs) continue;
                gen.restTimer = 0.0f;

                int slotsFree = gen.maxNpc - gen.aliveCount;
                int toSpawn = (std::min)(slotsFree, gen.maxPerGen > 0 ? gen.maxPerGen : 1);

                uint32_t meshId = 0;
                std::string mobName = "Monster";
                int maxHp = 1000;
                const MonsterDef* mDefPtr = nullptr;
                for (const auto& mDef : m_monsterDB) {
                    if (mDef.id == gen.npcType) {
                        meshId = mDef.meshId;
                        mobName = mDef.name;
                        maxHp = mDef.maxLife;
                        mDefPtr = &mDef;
                        break;
                    }
                }
                if (meshId == 0) continue; // npctype nao encontrado em dbmonster.txt

                for (int s = 0; s < toSpawn; s++) {
                    ExpandedMonsterEntity newMob;
                    newMob.originGeneratorId = gen.id;
                    newMob.meshId = meshId;
                    newMob.uid = m_nextMonsterUid++;

                    if (gen.bornX != 0 || gen.bornY != 0) {
                        newMob.mapX = (float)gen.bornX + 0.5f;
                        newMob.mapY = (float)gen.bornY + 0.5f;
                    }
                    else {
                        int rx = (gen.boundCx > 0) ? (rand() % gen.boundCx) : 0;
                        int ry = (gen.boundCy > 0) ? (rand() % gen.boundCy) : 0;
                        newMob.mapX = (float)(gen.boundX + rx) + 0.5f;
                        newMob.mapY = (float)(gen.boundY + ry) + 0.5f;
                    }
                    newMob.startX = newMob.mapX;
                    newMob.startY = newMob.mapY;

                    newMob.maxHp = (maxHp > 0) ? maxHp : 1000;
                    newMob.hp = newMob.maxHp;
                    newMob.visualHp = (float)newMob.maxHp;

                    // [BornAction/BornEffect/BornSound - ini\Monster.txt] Toca uma vez ao nascer
                    // (ex.: 315), depois o Draw3D detecta o fim da animacao e volta para StandBy (100).
                    newMob.currentAction = mDefPtr ? (int)mDefPtr->bornAction : 100;
                    newMob.hasPlayedBornAction = false;

                    std::wstring wName(mobName.begin(), mobName.end());
                    auto texData = Game::GenerateTextTexture(m_renderer, wName, RGB(255, 255, 255));
                    newMob.nameTexId = std::get<0>(texData);
                    newMob.nameW = std::get<1>(texData);
                    newMob.nameH = std::get<2>(texData);

                    m_monsters.push_back(newMob);
                    gen.aliveCount++;

                    if (mDefPtr && !mDefPtr->bornEffect.empty() && mDefPtr->bornEffect != "none" && mDefPtr->bornEffect != "None") {
                        size_t beforeCount = m_activeEffects.size();
                        LoadEffect(mDefPtr->bornEffect, newMob.mapX, newMob.mapY);
                        if (m_activeEffects.size() > beforeCount) {
                            m_activeEffects.back().attachedMonsterUid = newMob.uid;
                        }
                    }
                    if (mDefPtr && !mDefPtr->bornSound.empty() && mDefPtr->bornSound != "none" && mDefPtr->bornSound != "None") {
                        m_audio.PlaySoundEffect(m_clientPath + "\\" + mDefPtr->bornSound);
                    }
                }
            }
        }

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

                        ExpandedMonsterEntity faisao;
                        faisao.originGeneratorId = monster.originGeneratorId;
                        faisao.uid = m_nextMonsterUid++;
                        faisao.mapX = m_player.mapX + (float)((rand() % 12) - 6);
                        faisao.mapY = m_player.mapY + (float)((rand() % 12) - 6);
                        faisao.startX = faisao.mapX;
                        faisao.startY = faisao.mapY;
                        faisao.meshId = monster.meshId;

                        std::string mobName = "Monster";
                        const MonsterDef* mDefPtr = nullptr;
                        for (const auto& mDef : m_monsterDB) {
                            if (mDef.meshId == monster.meshId) {
                                mobName = mDef.name;
                                faisao.maxHp = mDef.maxLife;
                                mDefPtr = &mDef;
                                break;
                            }
                        }
                        if (faisao.maxHp <= 0) faisao.maxHp = 1000;
                        faisao.hp = faisao.maxHp;
                        faisao.visualHp = (float)faisao.maxHp;
                        faisao.currentAction = mDefPtr ? (int)mDefPtr->bornAction : 100;
                        faisao.hasPlayedBornAction = false;

                        std::wstring wName(mobName.begin(), mobName.end());
                        auto texData = Game::GenerateTextTexture(m_renderer, wName, RGB(255, 255, 255));
                        faisao.nameTexId = std::get<0>(texData);
                        faisao.nameW = std::get<1>(texData);
                        faisao.nameH = std::get<2>(texData);

                        spawnedMonsters.push_back(faisao);
                        it = m_monsters.erase(it);

                        if (mDefPtr && !mDefPtr->bornEffect.empty() && mDefPtr->bornEffect != "none" && mDefPtr->bornEffect != "None") {
                            size_t beforeCount = m_activeEffects.size();
                            LoadEffect(mDefPtr->bornEffect, faisao.mapX, faisao.mapY);
                            if (m_activeEffects.size() > beforeCount) {
                                m_activeEffects.back().attachedMonsterUid = faisao.uid;
                            }
                        }
                        if (mDefPtr && !mDefPtr->bornSound.empty() && mDefPtr->bornSound != "none" && mDefPtr->bornSound != "None") {
                            m_audio.PlaySoundEffect(m_clientPath + "\\" + mDefPtr->bornSound);
                        }
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

            // [Monstros parados] Nao vagam pelo mapa; ficam parados na posicao de
            // nascimento. So a animacao de nascimento (BornAction) toca uma vez e depois
            // volta para StandBy (100), que fica em loop.
            if (!monster.hasPlayedBornAction) {
                monster.animTimer += deltaTime;
                if (monster.animTimer >= (1.0f / 15.0f)) {
                    monster.currentFrame++;
                    monster.animTimer -= (1.0f / 15.0f);

                    auto& render = GetMonsterRender(monster.meshId, monster.currentAction);
                    int maxFrames = 1;
                    if (render.model.isValid && !render.model.motions.empty()) {
                        maxFrames = render.model.motions[0].frameCount;
                    }
                    if (maxFrames <= 0) maxFrames = 1;

                    if (monster.currentFrame >= maxFrames) {
                        monster.currentFrame = 0;
                        monster.hasPlayedBornAction = true;
                        monster.currentAction = 100; // StandBy
                    }
                }
            }
            else {
                monster.animTimer += deltaTime;
                if (monster.animTimer >= (1.0f / 8.0f)) {
                    monster.currentFrame++;
                    monster.animTimer -= (1.0f / 8.0f);

                    auto& render = GetMonsterRender(monster.meshId, monster.currentAction);
                    int maxFrames = 1;
                    if (render.model.isValid && !render.model.motions.empty()) {
                        maxFrames = render.model.motions[0].frameCount;
                    }
                    if (maxFrames <= 0) maxFrames = 1;
                    if (monster.currentFrame >= maxFrames) monster.currentFrame = 0;
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

            // [Efeito preso ao monstro - BornEffect] Recalcula mapX/mapY a partir da
            // posicao atual do monstro dono a cada frame, entao se ele se mover o efeito
            // acompanha (removido automaticamente se o monstro nao existir mais).
            if (effect.attachedMonsterUid != 0) {
                bool ownerFound = false;
                for (auto& mob : m_monsters) {
                    if (mob.uid == effect.attachedMonsterUid) {
                        effect.mapX = mob.mapX;
                        effect.mapY = mob.mapY;
                        ownerFound = true;
                        break;
                    }
                }
                if (!ownerFound) {
                    effect.isFinished = true;
                }
            }

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
                        if (part.model.isValid) maxFrames = (std::max)(maxFrames, GetModelMaxFrame(part.model));
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

        m_cameraX = worldX - (m_window.m_width / 2.0f) + m_cameraOffsetX;
        m_cameraY = worldY - (m_window.m_height / 2.0f) + m_cameraOffsetY;

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

                    SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
                    m_renderer.DrawSprite(m_puzzleTextures[tileId], (int)zX, (int)zY, (int)std::ceil(zW) + 1, (int)std::ceil(zW) + 1);
                }
            }
        }

        // [TESTE] Desenha o campeao Riot de teste (controlado via ImGui) fixo no mapa 1002, coordenada (444,444).
        if (GraphicsRiot::IsRiotModelValid(m_riotTestHandle)) {
            auto [riotWorldX, riotWorldY] = coordSystem.MapToScreen(51.0f, 51.0f);
            float riotDrawX = riotWorldX - m_cameraX;
            float riotDrawY = riotWorldY - m_cameraY;
            float riotZx = cx + (riotDrawX - cx) * m_zoom;
            float riotZy = cy + (riotDrawY - cy) * m_zoom;
            float riotAngleRad = m_riotAngleDeg * (3.14159265358979323846f / 180.0f);
            GraphicsRiot::DrawRiotModel(m_riotTestHandle, riotZx, riotZy, riotAngleRad, m_zoom * m_riotScale, 1.0f);

            // Barra de vida (azul, conforme solicitado) e nome do personagem acima da cabeca.
            if (m_texHpBlack != -1 && m_texHpBlueRiot != -1) {
                int riotHpBarW = 40; int riotHpBarH = 4;
                int hpX = (int)riotZx - (int)((riotHpBarW * m_zoom) / 2);
                int hpY = (int)riotZy - (int)(110 * m_zoom);

                SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
                m_renderer.DrawSprite(m_texHpBlack, hpX - 1, hpY - 1, (int)(riotHpBarW * m_zoom) + 2, (int)(riotHpBarH * m_zoom) + 2);

                float riotHpRatio = m_riotHpRatio;
                if (riotHpRatio < 0.0f) riotHpRatio = 0.0f;
                if (riotHpRatio > 1.0f) riotHpRatio = 1.0f;
                int riotHpW = (int)(riotHpBarW * riotHpRatio);
                if (riotHpW > 0) m_renderer.DrawSprite(m_texHpBlueRiot, hpX, hpY, (int)(riotHpW * m_zoom), (int)(riotHpBarH * m_zoom));

                if (m_riotNameTexId != -1) {
                    int nameX = (int)riotZx - (m_riotNameW / 2);
                    int nameY = hpY - m_riotNameH - 2;
                    m_renderer.DrawSprite(m_riotNameTexId, nameX, nameY, m_riotNameW, m_riotNameH);
                }
            }
        }

        // [TESTE] Segundo campeao Riot (painel "Riot Champion 2 (LoL)"), carregado a partir do
        // catalogo lol_personagens.ini. Desenhado ao lado do primeiro, no mesmo mapa de teste.
        if (m_riot2Enabled && GraphicsRiot::IsRiotModelValid(m_riot2Handle)) {
            auto [riot2WorldX, riot2WorldY] = coordSystem.MapToScreen(53.0f, 53.0f);
            float riot2DrawX = riot2WorldX - m_cameraX;
            float riot2DrawY = riot2WorldY - m_cameraY;
            float riot2Zx = cx + (riot2DrawX - cx) * m_zoom;
            float riot2Zy = cy + (riot2DrawY - cy) * m_zoom;
            float riot2AngleRad = m_riot2AngleDeg * (3.14159265358979323846f / 180.0f);
            GraphicsRiot::DrawRiotModel(m_riot2Handle, riot2Zx, riot2Zy, riot2AngleRad, m_zoom * m_riot2Scale, 1.0f);

            if (m_texHpBlack != -1 && m_texHpBlueRiot != -1) {
                int riot2HpBarW = 40; int riot2HpBarH = 4;
                int hpX2 = (int)riot2Zx - (int)((riot2HpBarW * m_zoom) / 2);
                int hpY2 = (int)riot2Zy - (int)(110 * m_zoom);

                SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
                m_renderer.DrawSprite(m_texHpBlack, hpX2 - 1, hpY2 - 1, (int)(riot2HpBarW * m_zoom) + 2, (int)(riot2HpBarH * m_zoom) + 2);

                float riot2HpRatio = m_riot2HpRatio;
                if (riot2HpRatio < 0.0f) riot2HpRatio = 0.0f;
                if (riot2HpRatio > 1.0f) riot2HpRatio = 1.0f;
                int riot2HpW = (int)(riot2HpBarW * riot2HpRatio);
                if (riot2HpW > 0) m_renderer.DrawSprite(m_texHpBlueRiot, hpX2, hpY2, (int)(riot2HpW * m_zoom), (int)(riot2HpBarH * m_zoom));

                if (m_riot2NameTexId != -1) {
                    int nameX2 = (int)riot2Zx - (m_riot2NameW / 2);
                    int nameY2 = hpY2 - m_riot2NameH - 2;
                    m_renderer.DrawSprite(m_riot2NameTexId, nameX2, nameY2, m_riot2NameW, m_riot2NameH);
                }
            }
        }


        struct RenderNode { float depth; int type; int index; };
        std::vector<RenderNode> renderQueue;
        renderQueue.push_back({ m_player.mapX + m_player.mapY, 0, 0 });
        // [Range de visualizacao] So desenha monstros/NPCs dentro de m_entityViewRange
        // celulas do boneco (distancia euclidiana), evitando poluir a tela quando muitos
        // geradores estao ativos ao mesmo tempo.
        for (size_t i = 0; i < m_monsters.size(); i++) {
            float ddx = m_monsters[i].mapX - m_player.mapX;
            float ddy = m_monsters[i].mapY - m_player.mapY;
            if (std::sqrt(ddx * ddx + ddy * ddy) > m_entityViewRange) continue;
            renderQueue.push_back({ m_monsters[i].mapX + m_monsters[i].mapY, 1, (int)i });
        }
        for (size_t i = 0; i < m_sceneObjects.size(); i++) renderQueue.push_back({ m_sceneObjects[i].depthKey, 2, (int)i });
        for (size_t i = 0; i < m_npcs.size(); i++) {
            float ddx = m_npcs[i].mapX - m_player.mapX;
            float ddy = m_npcs[i].mapY - m_player.mapY;
            if (std::sqrt(ddx * ddx + ddy * ddy) > m_entityViewRange) continue;
            renderQueue.push_back({ m_npcs[i].mapX + m_npcs[i].mapY, 3, (int)i });
        }
        std::sort(renderQueue.begin(), renderQueue.end(), [](const RenderNode& a, const RenderNode& b) { return a.depth < b.depth; });

        for (const auto& node : renderQueue) {
            if (node.type == 0) {

                float pDrawX = worldX - m_cameraX;
                float pDrawY = worldY - m_cameraY;
                float pZx = cx + (pDrawX - cx) * m_zoom;
                float pZy = cy + (pDrawY - cy) * m_zoom;

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
                    else if (m_player.isInWater) mainBodyModel = &(*activeBodyParts)[0].swimModel;
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
                    else if (m_player.isInWater) activeModel = &part.swimModel;
                    else if (m_player.isMoving) activeModel = m_isRunning ? &part.runModel : &part.walkModel;
                    else if (m_player.isAlert) activeModel = &part.alertModel;

                    if (activeModel->isValid)
                        m_renderer.DrawMesh3D(*activeModel, pZx, pZy - (m_player.jumpZ * m_zoom), part.textureId, m_player.currentFrame, m_player.facingAngle, m_playerPitch, true, m_zoom, nullptr, -1, 0, part.asb, part.adb, 1.0f, false, 0);
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
                    else if (m_player.isInWater) { activeHair = &m_hairSwimModel; }
                    else if (m_player.isMoving) { activeHair = m_isRunning ? &m_hairRunModel : &m_hairWalkModel; }
                    else if (m_player.isAlert) { activeHair = &m_hairAlertModel; }

                    if (activeHair->isValid) m_renderer.DrawMesh3D(*activeHair, pZx, pZy - (m_player.jumpZ * m_zoom), m_hairTextureId, m_player.currentFrame, m_player.facingAngle, m_playerPitch, false, m_zoom);
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
                        else if (m_player.isInWater) activeModel = &part.swimModel;
                        else if (m_player.isMoving) activeModel = m_isRunning ? &part.runModel : &part.walkModel;
                        else if (m_player.isAlert) activeModel = &part.alertModel;

                        if (activeModel->isValid)
                            m_renderer.DrawMesh3D(*activeModel, pZx, pZy - (m_player.jumpZ * m_zoom), part.textureId, m_player.currentFrame, m_player.facingAngle, m_playerPitch, false, m_zoom, nullptr, -1, 0, part.asb, part.adb, 1.0f, false, 0);
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
                        m_renderer.DrawMesh3D(wPart.model, pZx, pZy - (m_player.jumpZ * m_zoom), wPart.textureId, m_player.currentFrame, m_player.facingAngle, m_playerPitch, false, m_zoom, mainBodyModel, rightWeaponBone, m_player.currentFrame, wPart.asb, wPart.adb, 1.0f, false, 0);

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
                                        m_renderer.DrawMesh3D(ep.model, pZx, pZy - (m_player.jumpZ * m_zoom), ep.textureId, meshFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, rightWeaponBone, m_player.currentFrame, easb, eadb, 1.0f, true, eColor);
                                    }

                                    if (!ep.model.ptcls.empty()) {
                                        int ptclFrame = m_weaponEffectFrame % ep.model.ptcls[0].frames.size();
                                        m_renderer.DrawParticles(ep.model, pZx, pZy - (m_player.jumpZ * m_zoom), ep.textureId, ptclFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, rightWeaponBone, m_player.currentFrame, eColor);
                                    }

                                    if (m_player.isAttacking && !ep.model.shapes.empty()) {
                                        m_renderer.DrawShapes(ep.model, wPart.shapeStates[i], pZx, pZy - (m_player.jumpZ * m_zoom), ep.textureId, m_weaponEffectFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, rightWeaponBone, m_player.currentFrame, eColor, false);
                                    }
                                }
                            }
                        }
                    }
                }

                for (auto& wPart : m_leftWeaponParts) {
                    if (wPart.model.isValid && mainBodyModel) {
                        m_renderer.DrawMesh3D(wPart.model, pZx, pZy - (m_player.jumpZ * m_zoom), wPart.textureId, m_player.currentFrame, m_player.facingAngle, m_playerPitch, false, m_zoom, mainBodyModel, leftWeaponBone, m_player.currentFrame, wPart.asb, wPart.adb, 1.0f, false, 0);

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
                                        m_renderer.DrawMesh3D(ep.model, pZx, pZy - (m_player.jumpZ * m_zoom), ep.textureId, meshFrame, m_player.facingAngle, 0.0f, false, m_zoom, mainBodyModel, leftWeaponBone, m_player.currentFrame, easb, eadb, 1.0f, true, eColor);
                                    }

                                    if (!ep.model.ptcls.empty()) {
                                        int ptclFrame = m_weaponEffectFrame % ep.model.ptcls[0].frames.size();
                                        m_renderer.DrawParticles(ep.model, pZx, pZy - (m_player.jumpZ * m_zoom), ep.textureId, ptclFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, leftWeaponBone, m_player.currentFrame, eColor);
                                    }

                                    if (m_player.isAttacking && !ep.model.shapes.empty()) {
                                        m_renderer.DrawShapes(ep.model, wPart.shapeStates[i], pZx, pZy - (m_player.jumpZ * m_zoom), ep.textureId, m_weaponEffectFrame, m_player.facingAngle, 0.0f, m_zoom, easb, eadb, mainBodyModel, leftWeaponBone, m_player.currentFrame, eColor, false);
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
                                m_renderer.DrawMesh3D(part.model, pZx + wingOffsetX, pZy - (m_player.jumpZ * m_zoom) - wingOffsetY, part.textureId, meshFrame, wingRotation, wingPitch, false, m_zoom, mainBodyModel, attachBone, m_player.currentFrame, asb, adb, 1.0f, true, 0);
                            }
                            if (!part.model.ptcls.empty()) {
                                int ptclFrame = m_wingFrame % part.model.ptcls[0].frames.size();
                                m_renderer.DrawParticles(part.model, pZx + wingOffsetX, pZy - (m_player.jumpZ * m_zoom) - wingOffsetY, part.textureId, ptclFrame, wingRotation, wingPitch, m_zoom, asb, adb, mainBodyModel, attachBone, m_player.currentFrame, 0);
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

                        SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
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

                SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
                m_renderer.DrawSprite(obj.textureId, (int)zX, (int)zY, (int)zW, (int)zH);
            }
            else if (node.type == 3) {
                auto& npc = m_npcs[node.index];
                auto& render = GetNpcRender((uint32_t)npc.cacheIndex);

                auto [nWorldX, nWorldY] = coordSystem.MapToScreen(npc.mapX, npc.mapY);
                float drawX = nWorldX - m_cameraX; float drawY = nWorldY - m_cameraY;
                float zX = cx + (drawX - cx) * m_zoom; float zY = cy + (drawY - cy) * m_zoom;

                if (render.isMonsterStyle) {
                    auto& monsterRender = GetMonsterRender(render.monsterMeshId, 100);
                    if (monsterRender.model.isValid) {
                        m_renderer.DrawMesh3D(monsterRender.model, zX, zY, monsterRender.textureId, npc.currentFrame, npc.facingAngle, 0.0f, false, m_zoom, nullptr, -1, 0, monsterRender.asb, monsterRender.adb, 1.0f, false, 0);
                    }
                }
                else {
                    const std::vector<Resource::C3Model>* activeParts = &render.partModels;
                    if (npc.animState == 1) activeParts = &render.partModelsRest;
                    else if (npc.animState == 2) activeParts = &render.partModelsBlaze;
                    if (activeParts->empty() || activeParts->size() != render.partModels.size()) activeParts = &render.partModels;

                    for (size_t p = 0; p < activeParts->size(); p++) {
                        if ((*activeParts)[p].isValid) {
                            m_renderer.DrawMesh3D((*activeParts)[p], zX, zY, render.partTextureIds[p], npc.currentFrame, npc.facingAngle, 0.0f, false, m_zoom, nullptr, -1, 0, render.asb, render.adb, 1.0f, false, 0);
                        }
                    }
                }

                if (npc.nameTexId != -1) {
                    int nameX = (int)zX - (npc.nameW / 2);
                    int nameY = (int)zY - (int)(110 * m_zoom) - npc.nameH - 2;
                    SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
                    m_renderer.DrawSprite(npc.nameTexId, nameX, nameY, npc.nameW, npc.nameH);
                }
            }
        }

        for (auto& effect : m_activeEffects) {
            if (effect.isWaitingDelay || effect.isWaitingInterval || effect.isFinished) continue;

            // [Range de visualizacao] Efeitos presos ao mapa (nascimento de monstro, etc.)
            // tambem respeitam m_entityViewRange; efeitos fixos na tela (isScreenFixed, ex.
            // efeito de regiao/cidade) ou sem posicao de mapa nunca sao filtrados.
            if (!effect.isScreenFixed && effect.mapX != -1.0f && effect.mapY != -1.0f) {
                float edx = effect.mapX - m_player.mapX;
                float edy = effect.mapY - m_player.mapY;
                if (std::sqrt(edx * edx + edy * edy) > m_entityViewRange) continue;
            }

            float drawCx = cx;
            float drawCy = cy - (effect.isScreenFixed ? 0.0f : (m_player.jumpZ * m_zoom));

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
                if (part.model.isValid) globalMaxFrames = (std::max)(globalMaxFrames, GetModelMaxFrame(part.model));
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

                if (part.model.isValid) {
                    if (!part.model.phys.empty()) {
                        bool drawMesh = true;
                        if (effect.config.loopTime != 0 && !part.model.motions.empty()) {
                            if (effect.currentFrame >= part.model.motions[0].frameCount) {
                                drawMesh = false;
                            }
                        }
                        if (drawMesh) {
                            m_renderer.DrawMesh3D(part.model, drawCx, drawCy, part.textureId, effect.currentFrame, effect.angle, ePitch, false, eScale, nullptr, -1, 0, asb, adb, eAlpha, true, eColor);
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
                            m_renderer.DrawParticles(part.model, drawCx, drawCy, part.textureId, ptclFrame, effect.angle, ePitch, eScale, asb, adb, nullptr, -1, 0, eColor);
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

        if (m_texProgressHP != -1 && m_texProgressMP != -1) {
            SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
            int globeSize = 134;
            int globeLifeX = startX + 2;
            int globeManaX = startX + 50;
            int globeY = screenBottom - 84;

            m_renderer.DrawSprite(m_texProgressHP, globeLifeX, globeY, globeSize, globeSize);
            m_renderer.DrawSprite(m_texProgressMP, globeManaX, globeY, globeSize, globeSize);
        }

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

        if (m_texDialogTalk1 != -1 && m_texDialogTalk2 != -1 && m_texDialogTalk3 != -1 && m_texDialogTalk4 != -1) {
            SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
            int talkY = screenBottom - 74;
            int distanciaXChat = 82;

            m_renderer.DrawSprite(m_texDialogTalk1, startX + distanciaXChat, talkY, 256, 32);
            m_renderer.DrawSprite(m_texDialogTalk2, startX + distanciaXChat + 256, talkY, 256, 32);
            m_renderer.DrawSprite(m_texDialogTalk3, startX + distanciaXChat + 512, talkY, 256, 32);
            m_renderer.DrawSprite(m_texDialogTalk4, startX + distanciaXChat + 768, talkY, 256, 32);
        }

        SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);

        int bootX = startX + 1;    int bootY = screenBottom - 118;
        int equipX = startX + 22;  int equipY = screenBottom - 130;
        int moveX = startX + 50;   int moveY = screenBottom - 130;
        int mapX = startX + 72;    int mapY = screenBottom - 118;

        if (m_texRunChk1 != -1 && m_texRunChk2 != -1) {
            m_renderer.DrawSprite(m_isRunning ? m_texRunChk2 : m_texRunChk1, bootX, bootY, 32, 32);
        }

        if (m_texNpcEquip1 != -1 && m_texNpcEquip2 != -1) {
            m_renderer.DrawSprite(m_isNpcEquipMode ? m_texNpcEquip2 : m_texNpcEquip1, equipX, equipY, 32, 32);
        }

        if (m_texScreenMove1 != -1 && m_texScreenMove2 != -1) {
            m_renderer.DrawSprite(m_isScreenMove ? m_texScreenMove2 : m_texScreenMove1, moveX, moveY, 32, 32);
        }

        if (m_texMapChk1 != -1 && m_texMapChk2 != -1) {
            m_renderer.DrawSprite(m_showMiniMap ? m_texMapChk2 : m_texMapChk1, mapX, mapY, 32, 32);
        }

        if (m_showMiniMap && !m_miniMapParts.empty()) {
            int mapSize = 128;

            int cols = std::ceil(std::sqrt(m_miniMapParts.size()));
            int rows = std::ceil((float)m_miniMapParts.size() / cols);

            int totalMiniMapW = cols * mapSize;
            int totalMiniMapH = rows * mapSize;

            int mapScreenX = m_window.m_width - totalMiniMapW;
            int mapScreenY = 0;

            for (size_t i = 0; i < m_miniMapParts.size(); i++) {
                int col = i % cols;
                int row = i / cols;
                int px = mapScreenX + (col * mapSize);
                int py = mapScreenY + (row * mapSize);
                m_renderer.DrawSprite(m_miniMapParts[i], px, py, mapSize, mapSize);
            }

            if (m_heroTgaId != -1 && m_currentDMap.isValid) {
                float nx = m_player.mapX / (float)m_currentDMap.width;
                float ny = m_player.mapY / (float)m_currentDMap.height;

                float isoX = (nx - ny) * 0.5f + 0.5f;
                float isoY = (nx + ny) * 0.5f;

                int hx = mapScreenX + (int)(isoX * totalMiniMapW) - 8;
                int hy = mapScreenY + (int)(isoY * totalMiniMapH) - 8;

                SetSpriteUV(0.0f, 1.0f, 1.0f, 0.0f);
                m_renderer.DrawSprite(m_heroTgaId, hx, hy, 16, 16);
                SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
            }

            // [region.ini] Nome discreto da regiao/cidade atual, exibido em cima do minimap.
            if (m_regionNameTexId != -1) {
                int nameX = mapScreenX + (totalMiniMapW - m_regionNameW) / 2;
                int nameY = mapScreenY + 4;
                m_renderer.DrawSprite(m_regionNameTexId, nameX, nameY, m_regionNameW, m_regionNameH);
            }
        }

        SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
        int magicBtnX = startX + 750;
        int magicBtnY = screenBottom - 48;

        if (m_selectedSkillId != 0 && m_skillIconTexIds.count(m_selectedSkillId)) {
            m_renderer.DrawSprite(m_skillIconTexIds[m_selectedSkillId], magicBtnX, magicBtnY, 64, 64);
        }
        else if (m_texMainImgMagic != -1) {
            m_renderer.DrawSprite(m_texMainImgMagic, magicBtnX, magicBtnY, 64, 64);
        }

        if (m_showSkillList) {
            int popupStartX = magicBtnX - 45;
            int popupStartY = magicBtnY - 140;

            if (m_texHpBlack != -1) m_renderer.DrawSprite(m_texHpBlack, popupStartX - 5, popupStartY - 5, 110, 135);

            int startIndex = m_skillCurrentPage * 6;
            for (int i = 0; i < 6; i++) {
                int skillIndex = startIndex + i;
                if (skillIndex >= m_uiSkills.size()) break;

                int col = i % 2;
                int row = i / 2;
                int sx = popupStartX + (col * 40);
                int sy = popupStartY + (row * 40);

                uint32_t sId = m_uiSkills[skillIndex].id;
                if (m_skillIconTexIds.count(sId)) {
                    m_renderer.DrawSprite(m_skillIconTexIds[sId], sx, sy, 32, 32);
                }

                if (m_mouseX >= sx && m_mouseX <= sx + 32 && m_mouseY >= sy && m_mouseY <= sy + 32) {
                    int tId = m_uiSkills[skillIndex].tooltipTexId;
                    if (tId != -1) {
                        if (m_texHpBlack != -1) m_renderer.DrawSprite(m_texHpBlack, m_mouseX + 15, m_mouseY, m_uiSkills[skillIndex].tooltipW + 10, m_uiSkills[skillIndex].tooltipH + 10);
                        m_renderer.DrawSprite(tId, m_mouseX + 20, m_mouseY + 5, m_uiSkills[skillIndex].tooltipW, m_uiSkills[skillIndex].tooltipH);
                    }
                }
            }

            int upX = popupStartX + 85; int upY = popupStartY + 10;
            int downX = popupStartX + 85; int downY = popupStartY + 100;
            if (m_texQuerySkillBtnU != -1) m_renderer.DrawSprite(m_texQuerySkillBtnU, upX, upY, 16, 16);
            if (m_texQuerySkillBtnD != -1) m_renderer.DrawSprite(m_texQuerySkillBtnD, downX, downY, 16, 16);
        }

        if (m_currentDMap.isValid && m_currentPul.isValid) {
            float cx = m_window.m_width / 2.0f;
            float cy = m_window.m_height / 2.0f;

            float pZx = cx - (m_cameraOffsetX * m_zoom);
            float pZy = cy - (m_cameraOffsetY * m_zoom);

            int hpBarW = 60, hpBarH = 6, mpBarH = 4;
            int headX = (int)pZx - (int)((hpBarW * m_zoom) / 2);
            int headY = (int)pZy - (m_player.jumpZ * m_zoom) - (130 * m_zoom);

            if (m_texHpBlack != -1 && m_texHpRed != -1 && m_texMpBlue != -1) {
                int totalH = hpBarH + mpBarH + 1;
                m_renderer.DrawSprite(m_texHpBlack, headX - 1, headY - 1, (int)(hpBarW * m_zoom) + 2, (int)(totalH * m_zoom) + 2);

                m_renderer.DrawSprite(m_texHpRed, headX, headY, (int)((hpBarW * 0.8f) * m_zoom), (int)(hpBarH * m_zoom));

                int mpY = headY + (int)((hpBarH + 1) * m_zoom);
                m_renderer.DrawSprite(m_texMpBlue, headX, mpY, (int)((hpBarW * 0.3f) * m_zoom), (int)(mpBarH * m_zoom));
            }

            if (m_player.nameTexId != -1) {
                int nameX = headX + (int)((hpBarW * m_zoom) / 2) - (m_player.nameW / 2);
                int nameY = headY - m_player.nameH - 5;
                m_renderer.DrawSprite(m_player.nameTexId, nameX, nameY, m_player.nameW, m_player.nameH);
            }
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
            auto texData = Game::GenerateTextTexture(m_renderer, currentDebugStr, RGB(255, 255, 255));
            m_debugTexId = std::get<0>(texData);
            m_debugTexW = std::get<1>(texData);
            m_debugTexH = std::get<2>(texData);
            m_lastDebugStr = currentDebugStr;
        }

        SetSpriteUV(0.0f, 0.0f, 1.0f, 1.0f);
        if (m_debugTexId != -1) m_renderer.DrawSprite(m_debugTexId, 10, 10, m_debugTexW, m_debugTexH);
    }

    void Run() {
        srand((unsigned int)time(NULL));
        MSG msg = {};
        auto lastTime = std::chrono::high_resolution_clock::now();
        // [Config.ini] FPSLimit=0 significa sem limite (deixa o VSync, se ativado, regular o ritmo).
        double minFrameSeconds = (m_configFpsLimit > 0) ? (1.0 / (double)m_configFpsLimit) : 0.0;
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg); DispatchMessage(&msg);
            }
            else {
                auto frameStart = std::chrono::high_resolution_clock::now();
                float deltaTime = std::chrono::duration<float>(frameStart - lastTime).count();
                lastTime = frameStart;
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

                // [Config.ini - FPSLimit] Se o quadro terminou mais rapido que o intervalo alvo,
                // dorme o restante do tempo para nao ultrapassar o limite configurado.
                if (minFrameSeconds > 0.0) {
                    double frameSeconds = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - frameStart).count();
                    double remaining = minFrameSeconds - frameSeconds;
                    if (remaining > 0.0) {
                        std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
                    }
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
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