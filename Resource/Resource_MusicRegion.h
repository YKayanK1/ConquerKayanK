// ============================================================================
// Conquer Kayank Engine - Map Music Region Parser (ini\MusicRegion.ini)
// ============================================================================
#pragma once
#include "Resource.h"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

namespace Resource {

	static std::vector<MusicRegionEntry> ParseMusicRegionData(const std::vector<uint8_t>& data) {
		std::vector<MusicRegionEntry> regions;
		if (data.empty()) return regions;

		std::string content((char*)data.data(), data.size());
		std::istringstream iss(content);
		std::string line;

		auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };

		MusicRegionEntry current;
		bool inSection = false;

		auto flush = [&]() {
			if (inSection) regions.push_back(current);
			};

		while (std::getline(iss, line)) {
			line.erase(0, line.find_first_not_of(" \r\n\t"));
			line.erase(line.find_last_not_of(" \r\n\t") + 1);
			if (line.empty() || line[0] == ';' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) continue;

			if (line[0] == '[' && line.back() == ']') {
				flush();
				current = MusicRegionEntry();
				inSection = true;
				continue;
			}

			if (!inSection) continue;

			size_t eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;

			std::string key = line.substr(0, eqPos);
			std::string val = line.substr(eqPos + 1);

			if (key == "Mapid") current.mapId = (uint32_t)safeStoi(val);
			else if (key == "Bound_X") current.boundX = safeStoi(val);
			else if (key == "Bound_Y") current.boundY = safeStoi(val);
			else if (key == "Bound_CX") current.boundCX = safeStoi(val);
			else if (key == "Bound_CY") current.boundCY = safeStoi(val);
			else if (key == "TitleMusic") current.titleMusic = (val == "NULL") ? "" : val;
			else if (key == "TitleMusicTime") current.titleMusicTime = safeStoi(val);
			else if (key == "Amount") current.amount = safeStoi(val);
			else if (key == "DelayTime") current.delayTime = safeStoi(val);
			else if (key.rfind("MusicTime", 0) == 0) {
				int idx = safeStoi(key.substr(9));
				if ((int)current.musicTimes.size() <= idx) current.musicTimes.resize(idx + 1);
				current.musicTimes[idx] = safeStoi(val);
			}
			else if (key.rfind("Music", 0) == 0) {
				int idx = safeStoi(key.substr(5));
				if ((int)current.musics.size() <= idx) current.musics.resize(idx + 1);
				current.musics[idx] = (val == "NULL") ? "" : val;
			}
		}
		flush();

		return regions;
	}
}
