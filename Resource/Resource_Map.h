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

    static DMapData ParseDMapData(const std::vector<uint8_t>& data) {
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
                    br.Skip(260); br.Skip(8);
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
                    br.Skip(4);
                    map.terrainObjects.push_back(obj);
                }
                else if (type == 10 || type == 19) { br.Skip(64); br.Skip(8); }
                else if (type == 15) { br.Skip(260); br.Skip(16); }
            }
        }

        map.isValid = true;
        return map;
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