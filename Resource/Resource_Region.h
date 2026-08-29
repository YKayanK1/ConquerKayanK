// ============================================================================
// Conquer Kayank Engine - Map Region Parser (ini\region.ini)
// ============================================================================
#pragma once
#include "Resource.h"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

namespace Resource {

	// Layout observado no arquivo real (13 colunas, separadas por espaco):
	// MapId Type X Y CX CY RegionName EffectName p1 p2 p3 p4 p5(sempre "none" quando sem efeito extra)
	static std::vector<MapRegionEntry> ParseRegionData(const std::vector<uint8_t>& data) {
		std::vector<MapRegionEntry> regions;
		if (data.empty()) return regions;

		std::string content((char*)data.data(), data.size());
		std::istringstream iss(content);
		std::string line;

		auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };

		while (std::getline(iss, line)) {
			line.erase(0, line.find_first_not_of(" \r\n\t"));
			line.erase(line.find_last_not_of(" \r\n\t") + 1);
			if (line.empty() || line[0] == ';' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) continue;

			std::istringstream ls(line);
			std::vector<std::string> tokens;
			std::string tok;
			while (ls >> tok) tokens.push_back(tok);
			if (tokens.size() < 8) continue;

			MapRegionEntry entry;
			entry.mapId = (uint32_t)safeStoi(tokens[0]);
			entry.type = safeStoi(tokens[1]);
			entry.x = safeStoi(tokens[2]);
			entry.y = safeStoi(tokens[3]);
			entry.cx = safeStoi(tokens[4]);
			entry.cy = safeStoi(tokens[5]);
			entry.regionName = tokens[6];
			entry.effectName = tokens[7];
			if (entry.effectName == "none" || entry.effectName == "NULL") entry.effectName.clear();
			if (entry.regionName == "none" || entry.regionName == "NULL") entry.regionName.clear();

			regions.push_back(entry);
		}

		return regions;
	}
}
