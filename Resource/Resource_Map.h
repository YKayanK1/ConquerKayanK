// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include "Resource.h"
#include "Resource_Utils.h"
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Resource {

    static std::unordered_map<uint32_t, GameMapRecord> ParseGameMapDatData(const std::vector<uint8_t>& data) {
        std::unordered_map<uint32_t, GameMapRecord> maps;
        if (data.empty()) return maps;
        BinaryReader br(data);
        uint32_t count = br.Read<uint32_t>();
        for (uint32_t i = 0; i < count; i++) {
            uint32_t id = br.Read<uint32_t>();
            uint32_t pathLen = br.Read<uint32_t>();
            std::string mapPath = br.ReadString(pathLen);
            uint32_t tileSize = br.Read<uint32_t>();
            GameMapRecord rec; rec.dmapPath = mapPath; rec.tileSize = tileSize;
            maps[id] = rec;
        }
        return maps;
    }

    static DMapData ParseDMapData(const std::vector<uint8_t>& data, bool isNewFormat = false) {
        DMapData map;
        if (data.empty()) return map;
        BinaryReader br(data);

        br.Skip(8);
        map.puzzleFile = br.ReadString(260);
        map.width = br.Read<uint32_t>();
        map.height = br.Read<uint32_t>();

        map.cells.resize(map.width * map.height);
        for (uint32_t y = 0; y < map.height; y++) {
            for (uint32_t x = 0; x < map.width; x++) {
                MapCell cell;
                cell.access = br.Read<int16_t>();
                cell.surface = br.Read<int16_t>();
                cell.elevation = br.Read<int16_t>();
                map.cells[y * map.width + x] = cell;
            }
            br.Skip(4);
        }

        if (br.CanRead(4)) {
            int portalCount = br.Read<int32_t>();
            for (int i = 0; i < portalCount; i++) {
                MapPortal p;
                p.mapX = br.Read<int32_t>();
                p.mapY = br.Read<int32_t>();
                p.portalIndex = br.Read<int32_t>();
                map.portals.push_back(p);
            }
        }

        if (br.CanRead(4)) {
            int objectCount = br.Read<int32_t>();
            for (int i = 0; i < objectCount; i++) {
                int type = br.Read<int32_t>();
                if (type == 0) type = br.Read<int32_t>();

                if (type == 1) {
                    // MAP_SCENE: aponta um modelo 3D de cena (ex.: pontes) por caminho, nao
                    // por indice. Antes esses bytes eram so pulados (perdidos); agora sao
                    // capturados em map.sceneObjects para poderem ser renderizados.
                    MapSceneObject scene;
                    scene.scenePath = br.ReadString(260);
                    scene.mapX = br.Read<int32_t>();
                    scene.mapY = br.Read<int32_t>();
                    if (!scene.scenePath.empty()) map.sceneObjects.push_back(scene);
                }
                else if (type == 4 || type == 24) {
                    MapTerrainObject obj;
                    obj.aniPath = br.ReadString(260);
                    obj.aniName = br.ReadString(128);
                    obj.mapX = br.Read<int32_t>();
                    obj.mapY = br.Read<int32_t>();
                    obj.width = br.Read<int32_t>();
                    obj.height = br.Read<int32_t>();
                    obj.offsetX = br.Read<int32_t>();
                    obj.offsetY = br.Read<int32_t>();
                    br.Skip(4); // interval
                    if (isNewFormat) br.Skip(4); // ShowWay (formato novo)
                    map.terrainObjects.push_back(obj);
                }
                else if (type == 10 || type == 19) {
                    br.Skip(64); br.Skip(8);
                    if (isNewFormat) br.Skip(24); // AnglePad/Vertical/Horizontal/ScaleX/Y/Z (6 floats)
                }
                else if (type == 15) { br.Skip(260); br.Skip(16); }
            }
        }

        // [Layers - objetos "flat" tipo ponte] Alem da secao classica de "Objects" acima,
        // o .dmap possui um bloco de camadas (layers) onde ficam objetos de terreno extras
        // (ex.: pontes) que nao aparecem na lista solta de objetos. Sem ler esse bloco, esses
        // objetos nunca sao criados/renderizados mesmo existindo no arquivo do mapa.
        //
        // [isNewFormat] Confirmado pelo ConquerMapViewer/MapFileLoader.LoadLayers: no formato
        // novo existe, ANTES do bloco classico de layers, um layer "sintetico" (lista flat de
        // objetos sem index/type/rateX/rateY) que nao estava sendo lido. Sem isso o parser
        // ficava desalinhado a partir daqui, corrompendo a leitura do bloco classico de layers
        // (onde tambem ficam as pontes) mesmo apos os fixes de ShowWay/AnglePad no bloco Objects.
        auto readLayerObject = [&](bool& stop) {
            if (!br.CanRead(4)) { stop = true; return; }
            int objType = br.Read<int32_t>();
            if (objType == 3 || objType == 4 || objType == 24) {
                // MAP_SCENE(3)/TerrainObject(4)/TerrainSectionCover(24): objeto de
                // terreno completo (aniPath+aniName+pos+tamanho+offset+interval[+ShowWay]).
                MapTerrainObject obj;
                obj.aniPath = br.ReadString(260);
                obj.aniName = br.ReadString(128);
                obj.mapX = br.Read<int32_t>();
                obj.mapY = br.Read<int32_t>();
                obj.width = br.Read<int32_t>();
                obj.height = br.Read<int32_t>();
                obj.offsetX = br.Read<int32_t>();
                obj.offsetY = br.Read<int32_t>();
                br.Skip(4); // interval
                if (isNewFormat) br.Skip(4); // ShowWay (formato novo)
                if (!obj.aniPath.empty()) map.terrainObjects.push_back(obj);
            }
            else if (objType == 1) {
                // Scene: layout diferente e menor (scenePath[260] + location[8]).
                // Capturado em map.sceneObjects (ex.: pontes) em vez de descartado.
                MapSceneObject scene;
                scene.scenePath = br.ReadString(260);
                scene.mapX = br.Read<int32_t>();
                scene.mapY = br.Read<int32_t>();
                if (!scene.scenePath.empty()) map.sceneObjects.push_back(scene);
            }
            else if (objType == 8) { br.Skip(260); }
            else if (objType == 10 || objType == 19) {
                br.Skip(64); br.Skip(8);
                if (isNewFormat) br.Skip(24); // AnglePad/Vertical/Horizontal/ScaleX/Y/Z (6 floats)
            }
            else if (objType == 15) { br.Skip(260); br.Skip(16); }
            else stop = true; // tipo desconhecido: para para nao ler lixo do arquivo
        };

        if (isNewFormat && br.CanRead(4)) {
            int flatCount = br.Read<int32_t>();
            bool stop = false;
            for (int i = 0; i < flatCount && !stop; i++) readLayerObject(stop);
        }

        if (br.CanRead(4)) {
            int layerCount = br.Read<int32_t>();
            for (int li = 0; li < layerCount; li++) {
                if (!br.CanRead(8)) break;
                br.Skip(4); // layer index
                int layerType = br.Read<int32_t>();
                if (layerType != 4) break; // so o tipo LAYER_SCENE e suportado/conhecido

                br.Skip(4); br.Skip(4); // rateX, rateY
                if (isNewFormat) { br.Skip(4); br.Skip(4); br.Skip(4); } // NewA/NewB/NewC (formato novo)

                if (!br.CanRead(4)) break;
                int objCount = br.Read<int32_t>();
                bool stop = false;
                for (int oi = 0; oi < objCount && !stop; oi++) readLayerObject(stop);
                if (stop) break;
            }
        }

        map.isValid = true;
        return map;
    }

    // [Scene file] Arquivo binario apontado por MapSceneObject::scenePath. Formato (confirmado
    // pelo ConquerMapViewer/SceneFileLoader): int32 count, depois por parte:
    // aniPath[256] + aniName[64] + imageOffset(int32,int32) + interval(int32) +
    // size(int32,int32) + thick(int32) + location(int32,int32) + height(int32) +
    // grid de celulas (size.width*size.height * (access:int32+surface:int32+height:int32)).
    static SceneFileData ParseSceneFileData(const std::vector<uint8_t>& data) {
        SceneFileData scene;
        if (data.empty()) return scene;
        BinaryReader br(data);
        if (!br.CanRead(4)) return scene;
        int count = br.Read<int32_t>();
        for (int i = 0; i < count; i++) {
            if (!br.CanRead(256 + 64 + 8 + 4 + 8 + 4 + 8 + 4)) break;
            SceneObjectPart part;
            part.aniPath = br.ReadString(256);
            part.aniName = br.ReadString(64);
            part.imageOffsetX = br.Read<int32_t>();
            part.imageOffsetY = br.Read<int32_t>();
            br.Skip(4); // interval
            int32_t sizeW = br.Read<int32_t>();
            int32_t sizeH = br.Read<int32_t>();
            br.Skip(4); // thick
            part.locationX = br.Read<int32_t>();
            part.locationY = br.Read<int32_t>();
            br.Skip(4); // height

            size_t safeW = (sizeW > 0) ? (size_t)sizeW : 0;
            size_t safeH = (sizeH > 0) ? (size_t)sizeH : 0;
            size_t cellBytes = safeW * safeH * 12;
            if (!br.CanRead(cellBytes)) break;
            br.Skip(cellBytes);

            if (!part.aniPath.empty()) scene.parts.push_back(part);
        }
        scene.isValid = true;
        return scene;
    }

    static PulData ParsePulData(const std::vector<uint8_t>& data) {
        PulData pul;
        if (data.empty()) return pul;
        BinaryReader br(data);

        pul.puzzleType = br.ReadString(8);
        pul.aniFile = br.ReadString(256);
        pul.horizontalTiles = br.Read<int32_t>();
        pul.verticalTiles = br.Read<int32_t>();

        pul.tiles.resize(pul.horizontalTiles * pul.verticalTiles);
        for (int y = 0; y < pul.verticalTiles; y++) {
            for (int x = 0; x < pul.horizontalTiles; x++) {
                pul.tiles[y * pul.horizontalTiles + x] = br.Read<int16_t>();
            }
        }
        pul.isValid = true;
        return pul;
    }

    static std::string ParseAniSectionData(const std::vector<uint8_t>& data, const std::string& sectionName) {
        if (data.empty()) return "";
        std::string content((char*)data.data(), data.size());
        std::istringstream iss(content);
        std::string line;
        bool inSection = false;
        std::string lowerSection = sectionName;
        for (auto& c : lowerSection) c = std::tolower((unsigned char)c);

        while (std::getline(iss, line)) {
            line.erase(0, line.find_first_not_of(" \r\n\t"));
            line.erase(line.find_last_not_of(" \r\n\t") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[' && line.back() == ']') {
                std::string currentSec = line.substr(1, line.size() - 2);
                for (auto& c : currentSec) c = std::tolower((unsigned char)c);
                inSection = (currentSec == lowerSection);
                continue;
            }

            if (inSection) {
                std::string lowerLine = line;
                for (auto& c : lowerLine) c = std::tolower((unsigned char)c);
                if (lowerLine.find("frame0=") == 0) {
                    return line.substr(7);
                }
            }
        }
        return "";
    }
}