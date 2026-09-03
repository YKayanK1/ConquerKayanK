#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

#ifndef RESOURCE_EXPORTS
#define RESOURCE_API __declspec(dllimport)
#else
#define RESOURCE_API __declspec(dllexport)
#endif


namespace Resource {

    struct RESOURCE_API TMENode {
        std::string effectName;
        uint32_t delay;
        uint32_t unknown1;
        uint32_t distance;
        uint32_t unknown2;
    };

    struct RESOURCE_API TMEData {
        bool isValid = false;
        std::vector<TMENode> nodes;
    };

    struct RESOURCE_API EffectPart {
        unsigned int effectId = 0;
        unsigned int textureId = 0;
        int asb = 5;
        int adb = 6;
    };

    struct RESOURCE_API EffectConfig {
        std::string name;
        int amount = 0;
        int delay = 0;
        int loopTime = 0;
        int frameInterval = 0;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        int loopInterval = 0;
        int colorEnable = 0;
        std::vector<EffectPart> parts;
    };

    struct RESOURCE_API ArmorPart {
        uint32_t mesh = 0;
        uint32_t texture = 0;
        int asb = 5;
        int adb = 6;
    };

    struct RESOURCE_API ArmorConfig {
        uint32_t id = 0;
        int partCount = 0;
        std::vector<ArmorPart> parts;
    };

    struct RESOURCE_API WeaponPart {
        uint32_t mesh = 0;
        uint32_t texture = 0;
        int asb = 5;
        int adb = 6;
    };

    struct RESOURCE_API WeaponConfig {
        uint32_t id = 0;
        int partCount = 0;
        std::vector<WeaponPart> parts;
    };

    struct RESOURCE_API Matrix4x4 { float m[16]; };
    struct RESOURCE_API Vec3 { float x, y, z; };

    struct RESOURCE_API PtclFrame {
        std::vector<Vec3> positions;
        std::vector<float> ages;
        std::vector<float> sizes;
        Matrix4x4 frameMatrix;
    };

    struct RESOURCE_API C3Ptcl {
        bool isPTC3 = false;
        std::string name;
        std::string textureName;
        int texRow = 1;
        float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
        float rotationSpeed = 0.0f;
        float maxAlpha = 1.0f, minAlpha = 0.0f;
        float totalLifetime = 1.0f, fadeStartAge = 0.0f, fadeEndAge = 0.8f;
        float initialAlpha = 1.0f, globalAlpha = 1.0f;
        int maxCount = 0;
        std::vector<PtclFrame> frames;
    };

    struct RESOURCE_API C3ShapeLine {
        std::vector<Vec3> points;
    };

    struct RESOURCE_API C3Shape {
        std::string name;
        std::string textureName;
        int segmentCount = 0;
        std::vector<C3ShapeLine> lines;
    };

    struct RESOURCE_API C3KeyFrame { int pos = 0; std::vector<Matrix4x4> boneMatrices; };
    struct RESOURCE_API C3Motion {
        int boneCount = 0, frameCount = 0, morphCount = 0;
        std::vector<C3KeyFrame> keyframes;
        std::vector<float> morphs;
    };
    struct RESOURCE_API PhyVertex {
        float px, py, pz, u, v;
        uint32_t boneIndex[2];
        float boneWeight[2];
    };

    struct RESOURCE_API C3Frame {
        int frame = 0;
        float fParam = 0.0f;
        bool bParam = false;
        int nParam = 0;
    };

    struct RESOURCE_API C3Phy {
        std::string name; std::string textureName; int blendCount = 0;
        uint32_t normalVertexCount = 0, alphaVertexCount = 0, normalTriCount = 0, alphaTriCount = 0;
        std::vector<PhyVertex> vertices; std::vector<uint16_t> indices;
        Matrix4x4 initMatrix; float bboxMin[3]; float bboxMax[3];

        // Texture atlas / keyframe animation data (drives multi-part effects like tornadoes)
        int texRow = 1;
        float uvStepX = 0.0f, uvStepY = 0.0f;
        bool twoSided = false;
        std::vector<C3Frame> alphaKeys;
        std::vector<C3Frame> drawKeys;
        std::vector<C3Frame> changeTexKeys;
    };

    // Mirrors C3Studio's Phy_Calculate alpha resolution: interpolates between
    // surrounding keyframes (or clamps to the nearest one at the ends).
    // Defined inline (free function) to avoid dllexport/inline-member linkage issues.
    inline bool C3Phy_ProcessAlpha(const C3Phy& phy, int frame, float& outAlpha) {
        int s = -1, e = -1;
        const auto& alphaKeys = phy.alphaKeys;
        for (int n = 0; n < (int)alphaKeys.size(); n++) {
            if (alphaKeys[n].frame <= frame) { if (s == -1 || n > s) s = n; }
            if (alphaKeys[n].frame > frame) { if (e == -1 || n < e) e = n; }
        }
        if (s == -1 && e > -1) { outAlpha = alphaKeys[e].fParam; return true; }
        if (s > -1 && e == -1) { outAlpha = alphaKeys[s].fParam; return true; }
        if (s > -1 && e > -1) {
            float t = (float)(frame - alphaKeys[s].frame) / (float)(alphaKeys[e].frame - alphaKeys[s].frame);
            outAlpha = alphaKeys[s].fParam + t * (alphaKeys[e].fParam - alphaKeys[s].fParam);
            return true;
        }
        return false;
    }

    inline bool C3Phy_ProcessDraw(const C3Phy& phy, int frame, bool& outVisible) {
        for (const auto& d : phy.drawKeys) if (d.frame == frame) { outVisible = d.bParam; return true; }
        return false;
    }

    inline bool C3Phy_ProcessChangeTex(const C3Phy& phy, int frame, int& outTexIndex) {
        for (const auto& c : phy.changeTexKeys) if (c.frame == frame) { outTexIndex = c.nParam; return true; }
        return false;
    }

    struct RESOURCE_API C3Model {
        bool isValid = false;
        std::vector<C3Phy> phys;
        std::vector<C3Motion> motions;
        std::vector<C3Ptcl> ptcls;
        std::vector<C3Shape> shapes;
    };

    struct RESOURCE_API MapCell { int16_t access, surface, elevation; };

    struct RESOURCE_API MapPortal {
        int mapX = 0, mapY = 0, portalIndex = 0;
    };

    struct RESOURCE_API MapTerrainObject {
        std::string aniPath;
        std::string aniName;
        int mapX = 0, mapY = 0;
        int width = 0, height = 0;
        int offsetX = 0, offsetY = 0;
    };

    // [MAP_SCENE] Objeto tipo "Scene" do .dmap: aponta por caminho (nao por indice) para um
    // modelo 3D de cena (ex.: pontes, estruturas), diferente de MapTerrainObject que e sprite
    // 2D animado. Guarda apenas o caminho do arquivo de cena e a posicao no mapa.
    struct RESOURCE_API MapSceneObject {
        std::string scenePath;
        int mapX = 0, mapY = 0;
    };

    struct RESOURCE_API DMapData {
        bool isValid = false;
        uint32_t width = 0, height = 0;
        std::string puzzleFile;
        std::vector<MapCell> cells;
        std::vector<MapPortal> portals;
        std::vector<MapTerrainObject> terrainObjects;
        std::vector<MapSceneObject> sceneObjects;
    };

    // [Scene file] Arquivo apontado por MapSceneObject::scenePath (ex.: pontes). Contem uma
    // lista de "partes" (sprites animados) posicionadas em celulas relativas ao MapSceneObject,
    // cada uma com seu proprio aniPath/aniName (textura) e offset em pixels na tela.
    struct RESOURCE_API SceneObjectPart {
        std::string aniPath;
        std::string aniName;
        int imageOffsetX = 0, imageOffsetY = 0;
        int locationX = 0, locationY = 0;
        // [Colisao/pulo] Dimensoes (em celulas) e grid de celulas (access/surface/elevation) do
        // proprio arquivo de cena (ex.: ponte). Necessario para aplicar a colisao/agua correta
        // do trecho da ponte sobre as celulas do mapa (que por padrao estao marcadas como agua
        // por baixo dela), permitindo tanto andar quanto pular sobre a ponte.
        int sizeW = 0, sizeH = 0;
        std::vector<MapCell> cells;
    };

    struct RESOURCE_API SceneFileData {
        bool isValid = false;
        std::vector<SceneObjectPart> parts;
    };

    struct RESOURCE_API PulData {
        bool isValid = false;
        std::string puzzleType, aniFile;
        int horizontalTiles = 0, verticalTiles = 0;
        std::vector<int16_t> tiles;
    };

    struct RESOURCE_API GameMapRecord {
        std::string dmapPath;
        uint32_t tileSize = 0;
    };

    // ini\MusicRegion.ini: define regioes retangulares (em tiles) de um mapa que
    // possuem musica de entrada (TitleMusic) e uma lista de musicas ambiente (MusicN)
    // que tocam em sequencia com DelayTime segundos entre elas.
    struct RESOURCE_API MusicRegionEntry {
        uint32_t mapId = 0;
        int boundX = 0, boundY = 0, boundCX = 0, boundCY = 0;
        std::string titleMusic;
        int titleMusicTime = 0;
        int amount = 0;
        std::vector<std::string> musics;
        std::vector<int> musicTimes;
        int delayTime = 0;

        bool Contains(int x, int y) const {
            return x >= boundX && x < boundX + boundCX && y >= boundY && y < boundY + boundCY;
        }
    };

    // ini\region.ini: define regioes retangulares (em tiles) de um mapa com um nome de
    // exibicao (mostrado sobre o minimap) e, opcionalmente, um efeito 3D (secao de
    // 3DEffect.ini) que e acionado enquanto o player estiver dentro do range.
    // Layout de cada linha: MapId Type X Y CX CY RegionName EffectName ... (campos finais ignorados)
    struct RESOURCE_API MapRegionEntry {
        uint32_t mapId = 0;
        int type = 0;
        int x = 0, y = 0, cx = 0, cy = 0;
        std::string regionName;
        std::string effectName;

        bool Contains(int px, int py) const {
            if (cx <= 0 || cy <= 0) return false; // sem bounds = entrada padrao/fallback, nao um range real
            return px >= x && px < x + cx && py >= y && py < y + cy;
        }
    };

    // ini\cq_npc.csv: banco de dados de NPCs do servidor (posicao/type/lookface).
    struct RESOURCE_API NpcDbEntry {
        uint32_t id = 0;
        std::string name;
        int type = 0;
        uint32_t lookface = 0;
        uint32_t mapId = 0;
        int cellX = 0, cellY = 0;
    };

    // ini\npc.ini [NpcTypeNNN]: usado quando lookfaceBase < 1000.
    struct RESOURCE_API NpcTypeConfig {
        std::string name;
        uint32_t simpleObjId = 0;
        uint32_t standByMotion = 0, blazeMotion = 0, restMotion = 0;
        std::string effect;
        int asb = 5, adb = 6;
        int fixDir = 0;
    };

    struct RESOURCE_API SimpleObjPart {
        uint32_t mesh = 0;
        uint32_t texture = 0;
    };

    // ini\3DSimpleObj.ini [ObjIDTypeNNNN]: partes de mesh/textura (sem animacao propria).
    struct RESOURCE_API SimpleObjConfig {
        int partAmount = 0;
        std::vector<SimpleObjPart> parts;
    };

    // ini\NpcX.ini [NNNN]: NPC montado como "boneco" completo (lookfaceBase >= 1000).
    struct RESOURCE_API NpcXConfig {
        std::string name;
        int addSize = 0;
        int scale = 100;
        int fixDir = 0;
        uint32_t look = 0;
        uint32_t head = 0, hair = 0;
        uint32_t armet = 0, armor = 0;
        uint32_t rWeapon = 0, lWeapon = 0;
        uint32_t misc = 0, mount = 0;
        std::string effect;
    };

    // ini\cq_generator.csv: geradores de monstros do servidor. Cada linha define uma area
    // retangular (bound_x,bound_y,bound_cx,bound_cy) de um mapa onde ate "maxnpc" monstros
    // do tipo "npctype" (== id em dbmonster.txt) podem existir simultaneamente. Quando um
    // morre, apos "rest_secs" o gerador pode repor ate "max_per_gen" por ciclo.
    struct RESOURCE_API GeneratorEntry {
        uint32_t id = 0;
        uint32_t mapId = 0;
        int boundX = 0, boundY = 0, boundCx = 0, boundCy = 0;
        int maxNpc = 0;
        int restSecs = 0;
        int maxPerGen = 0;
        uint32_t npcType = 0;
        int bornX = 0, bornY = 0;
    };

    class RESOURCE_API Manager {
    public:
        Manager();
        ~Manager();

        bool Initialize(const std::string& clientPath);
        std::vector<uint8_t> ReadLooseFile(const std::string& relativePath);
        std::vector<uint8_t> GetFileData(const std::string& internalPath);

        C3Model LoadC3Model(const std::string& c3Path);

        DMapData LoadDMap(const std::string& mapPath);
        SceneFileData LoadScene(const std::string& scenePath);
        PulData LoadPul(const std::string& pulPath);
        std::unordered_map<uint32_t, GameMapRecord> LoadGameMapDat(const std::string& path);
        std::string ParseAniSection(const std::string& aniPath, const std::string& sectionName);

        std::unordered_map<std::string, EffectConfig> Parse3DEffects(const std::string& filePath);
        std::unordered_map<uint32_t, std::string> ParseResIni(const std::string& filePath);

        std::unordered_map<uint32_t, ArmorConfig> ParseArmorIni(const std::string& filePath);
        std::unordered_map<uint32_t, WeaponConfig> ParseWeaponIni(const std::string& filePath);
        std::unordered_map<uint32_t, std::string> ParseAction3DEffects(const std::string& filePath);

        TMEData ParseTME(const std::string& filePath);

        std::unordered_map<std::string, std::string> ParseActionSound(const std::string& filePath);

        std::vector<MusicRegionEntry> ParseMusicRegions(const std::string& filePath);
        std::vector<MapRegionEntry> ParseRegions(const std::string& filePath);

        std::vector<NpcDbEntry> ParseNpcCsv(const std::string& filePath);
        std::unordered_map<uint32_t, NpcTypeConfig> ParseNpcTypeIni(const std::string& filePath);
        std::unordered_map<uint32_t, SimpleObjConfig> ParseSimpleObjIni(const std::string& filePath);
        std::unordered_map<uint32_t, NpcXConfig> ParseNpcXIni(const std::string& filePath);
        std::vector<GeneratorEntry> ParseGeneratorCsv(const std::string& filePath);

    private:
        struct Impl;
        Impl* pImpl;
    };
}