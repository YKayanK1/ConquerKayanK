// ============================================================================
// Conquer Kayank Engine - NPC Data Parsers
// ============================================================================
// Le a base de NPCs do servidor (ini\cq_npc.csv) e os arquivos de configuracao
// visual do cliente (ini\npc.ini, ini\3DSimpleObj.ini, ini\NpcX.ini) usados para
// resolver qual modelo 3D/textura/animacao cada NPC deve exibir.
#pragma once
#include "Resource.h"
#include "Resource_Utils.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Resource {

	// ------------------------------------------------------------------
	// ini\cq_npc.csv: banco de dados de NPCs (formato CSV com aspas, separado
	// por virgula). Colunas relevantes: id, name, type, lookface, mapid, cellx, celly.
	// type==1 -> invoca dialogo; type==2 -> npc que fala (dialogo simples).
	// ------------------------------------------------------------------
	static std::vector<NpcDbEntry> ParseNpcCsvData(const std::vector<uint8_t>& data) {
		std::vector<NpcDbEntry> npcs;
		if (data.empty()) return npcs;

		std::string content((char*)data.data(), data.size());
		std::istringstream iss(content);
		std::string line;

		std::getline(iss, line); // cabecalho: descarta

		auto splitCsv = [](const std::string& row) {
			std::vector<std::string> fields;
			std::string field;
			bool inQuotes = false;
			for (size_t i = 0; i < row.size(); i++) {
				char c = row[i];
				if (c == '"') { inQuotes = !inQuotes; continue; }
				if (c == ',' && !inQuotes) { fields.push_back(field); field.clear(); continue; }
				field += c;
			}
			fields.push_back(field);
			return fields;
			};

		auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };
		auto safeStou = [](const std::string& s) -> uint32_t { try { return (uint32_t)std::stoul(s); } catch (...) { return 0; } };

		while (std::getline(iss, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;

			auto fields = splitCsv(line);
			// id, ownerid, playerid, name, type, lookface, idxserver, mapid, cellx, celly, ...
			if (fields.size() < 10) continue;

			NpcDbEntry entry;
			entry.id = safeStou(fields[0]);
			entry.name = fields[3];
			entry.type = safeStoi(fields[4]);
			entry.lookface = safeStou(fields[5]);
			entry.mapId = safeStou(fields[7]);
			entry.cellX = safeStoi(fields[8]);
			entry.cellY = safeStoi(fields[9]);

			if (entry.id == 0 && entry.name.empty()) continue;
			npcs.push_back(entry);
		}

		return npcs;
	}

	// ------------------------------------------------------------------
	// ini\npc.ini: [NpcTypeNNN] usado quando lookfaceBase (lookface sem o
	// ultimo digito de direcao) < 1000. Aponta para um SimpleObjID (3DSimpleObj.ini).
	// ------------------------------------------------------------------
	static std::unordered_map<uint32_t, NpcTypeConfig> ParseNpcTypeIniData(const std::vector<uint8_t>& data) {
		std::unordered_map<uint32_t, NpcTypeConfig> result;
		if (data.empty()) return result;

		std::string content((char*)data.data(), data.size());
		std::istringstream iss(content);
		std::string line;

		NpcTypeConfig current;
		uint32_t currentId = 0;
		bool inSection = false;

		auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };
		auto safeStou = [](const std::string& s) -> uint32_t { try { return (uint32_t)std::stoul(s); } catch (...) { return 0; } };
		const std::string prefix = "NpcType";

		while (std::getline(iss, line)) {
			line.erase(0, line.find_first_not_of(" \r\n\t"));
			line.erase(line.find_last_not_of(" \r\n\t") + 1);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;

			if (line[0] == '[' && line.back() == ']') {
				if (inSection && currentId != 0) result[currentId] = current;
				current = NpcTypeConfig();
				std::string secName = line.substr(1, line.size() - 2);
				if (secName.rfind(prefix, 0) == 0) {
					currentId = safeStou(secName.substr(prefix.size()));
					inSection = true;
				}
				else {
					currentId = 0;
					inSection = false;
				}
				continue;
			}

			if (!inSection) continue;

			size_t eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;
			std::string key = line.substr(0, eqPos);
			std::string val = line.substr(eqPos + 1);

			if (key == "Name") current.name = val;
			else if (key == "SimpleObjID") current.simpleObjId = safeStou(val);
			else if (key == "StandByMotion") current.standByMotion = safeStou(val);
			else if (key == "BlazeMotion") current.blazeMotion = safeStou(val);
			else if (key == "RestMotion") current.restMotion = safeStou(val);
			else if (key == "Effect") current.effect = val;
			else if (key == "ASB") current.asb = safeStoi(val);
			else if (key == "ADB") current.adb = safeStoi(val);
			else if (key == "FixDir") current.fixDir = safeStoi(val);
		}
		if (inSection && currentId != 0) result[currentId] = current;

		return result;
	}

	// ------------------------------------------------------------------
	// ini\3DSimpleObj.ini: [ObjIDTypeNNNN] com PartAmount/PartN(mesh id em
	// 3dobj.ini)/TextureN (id em 3dtexture.ini).
	// ------------------------------------------------------------------
	static std::unordered_map<uint32_t, SimpleObjConfig> ParseSimpleObjIniData(const std::vector<uint8_t>& data) {
		std::unordered_map<uint32_t, SimpleObjConfig> result;
		if (data.empty()) return result;

		std::string content((char*)data.data(), data.size());
		std::istringstream iss(content);
		std::string line;

		SimpleObjConfig current;
		uint32_t currentId = 0;
		bool inSection = false;

		auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };
		auto safeStou = [](const std::string& s) -> uint32_t { try { return (uint32_t)std::stoul(s); } catch (...) { return 0; } };
		const std::string prefix = "ObjIDType";

		while (std::getline(iss, line)) {
			line.erase(0, line.find_first_not_of(" \r\n\t"));
			line.erase(line.find_last_not_of(" \r\n\t") + 1);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;

			if (line[0] == '[' && line.back() == ']') {
				if (inSection && currentId != 0) result[currentId] = current;
				current = SimpleObjConfig();
				std::string secName = line.substr(1, line.size() - 2);
				if (secName.rfind(prefix, 0) == 0) {
					currentId = safeStou(secName.substr(prefix.size()));
					inSection = true;
				}
				else {
					currentId = 0;
					inSection = false;
				}
				continue;
			}

			if (!inSection) continue;

			size_t eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;
			std::string key = line.substr(0, eqPos);
			std::string val = line.substr(eqPos + 1);

			if (key == "PartAmount") {
				current.partAmount = safeStoi(val);
				current.parts.resize(current.partAmount);
			}
			else if (key.rfind("Part", 0) == 0 && key != "PartAmount") {
				int idx = safeStoi(key.substr(4));
				if (idx >= 0 && idx < (int)current.parts.size()) current.parts[idx].mesh = safeStou(val);
			}
			else if (key.rfind("Texture", 0) == 0) {
				int idx = safeStoi(key.substr(7));
				if (idx >= 0 && idx < (int)current.parts.size()) current.parts[idx].texture = safeStou(val);
			}
		}
		if (inSection && currentId != 0) result[currentId] = current;

		return result;
	}

	// ------------------------------------------------------------------
	// ini\NpcX.ini: [NNNN] usado quando lookfaceBase >= 1000. Monta o NPC como
	// um "boneco" completo (Look/Head/Hair/Armet/Armor/RWeapon/LWeapon/Effect).
	// ------------------------------------------------------------------
	static std::unordered_map<uint32_t, NpcXConfig> ParseNpcXIniData(const std::vector<uint8_t>& data) {
		std::unordered_map<uint32_t, NpcXConfig> result;
		if (data.empty()) return result;

		std::string content((char*)data.data(), data.size());
		std::istringstream iss(content);
		std::string line;

		NpcXConfig current;
		uint32_t currentId = 0;
		bool inSection = false;

		auto safeStoi = [](const std::string& s) -> int { try { return std::stoi(s); } catch (...) { return 0; } };
		auto safeStou = [](const std::string& s) -> uint32_t { try { return (uint32_t)std::stoul(s); } catch (...) { return 0; } };

		while (std::getline(iss, line)) {
			line.erase(0, line.find_first_not_of(" \r\n\t"));
			line.erase(line.find_last_not_of(" \r\n\t") + 1);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;

			if (line[0] == '[' && line.back() == ']') {
				if (inSection && currentId != 0) result[currentId] = current;
				current = NpcXConfig();
				currentId = safeStou(line.substr(1, line.size() - 2));
				inSection = currentId != 0;
				continue;
			}

			if (!inSection) continue;

			size_t eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;
			std::string key = line.substr(0, eqPos);
			std::string val = line.substr(eqPos + 1);

			if (key == "Name") current.name = val;
			else if (key == "AddSize") current.addSize = safeStoi(val);
			else if (key == "Scale") current.scale = safeStoi(val);
			else if (key == "FixDir") current.fixDir = safeStoi(val);
			else if (key == "Look") current.look = safeStou(val);
			else if (key == "Head") current.head = safeStou(val);
			else if (key == "Hair") current.hair = safeStou(val);
			else if (key == "Armet") current.armet = safeStou(val);
			else if (key == "Armor") current.armor = safeStou(val);
			else if (key == "RWeapon") current.rWeapon = safeStou(val);
			else if (key == "LWeapon") current.lWeapon = safeStou(val);
			else if (key == "Misc") current.misc = safeStou(val);
			else if (key == "Mount") current.mount = safeStou(val);
			else if (key == "Effect") current.effect = val;
		}
		if (inSection && currentId != 0) result[currentId] = current;

		return result;
	}

}
