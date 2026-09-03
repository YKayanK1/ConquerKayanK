#pragma once
// ============================================================================
// Graphics_Riot - Formatos binarios League of Legends (.skn/.skl/.anm)
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>
#include <cctype>
#include <DirectXMath.h>

namespace Riot {

	using namespace DirectX;

	// ------------------------------------------------------------------
	// .skn (mesh)
	// ------------------------------------------------------------------
	struct SknVertex {
		XMFLOAT3 position;
		uint8_t  boneIndices[4] = { 0,0,0,0 };
		float    weights[4] = { 0,0,0,0 };
		XMFLOAT3 normal;
		XMFLOAT2 uv;
	};

	struct SknSubMesh {
		std::string name;
		uint32_t vertexOffset = 0;
		uint32_t vertexCount = 0;
		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;
	};

	struct SknModel {
		std::vector<SknSubMesh> subMeshes;
		std::vector<SknVertex>  vertices;
		std::vector<uint16_t>   indices;
		bool valid = false;
	};

	// ------------------------------------------------------------------
	// .skl (skeleton)
	// ------------------------------------------------------------------
	struct SklBone {
		std::string name;
		uint32_t nameHash = 0;
		int16_t  id = -1;
		int16_t  parentId = -1;
		XMFLOAT3 localPosition = { 0,0,0 };
		XMFLOAT3 localScale = { 1,1,1 };
		XMFLOAT4 localRotation = { 0,0,0,1 }; // quaternion xyzw
		XMFLOAT4X4 globalMatrix = {};
		XMFLOAT4X4 inverseGlobalMatrix = {};
	};

	struct SklModel {
		std::vector<SklBone> bones;
		std::vector<uint16_t> influences; // remaps .skn vertex bone indices -> actual bone array index
		bool valid = false;

		int FindBoneIndex(uint32_t hash) const {
			for (size_t i = 0; i < bones.size(); ++i)
				if (bones[i].nameHash == hash) return (int)i;
			return -1;
		}

		int FindBoneIndexById(int16_t id) const {
			for (size_t i = 0; i < bones.size(); ++i)
				if (bones[i].id == id) return (int)i;
			return -1;
		}
	};

	// ------------------------------------------------------------------
	// .anm (animation)
	// ------------------------------------------------------------------
	template<typename T>
	struct AnmFrame {
		float time = 0.0f;
		T value{};
	};

	struct AnmBoneTrack {
		uint32_t boneNameHash = 0;
		std::vector<AnmFrame<XMFLOAT3>> translationFrames;
		std::vector<AnmFrame<XMFLOAT4>> rotationFrames; // quaternion xyzw
		std::vector<AnmFrame<XMFLOAT3>> scaleFrames;
	};

	struct AnmModel {
		float duration = 0.0f;
		float fps = 30.0f;
		std::vector<AnmBoneTrack> tracks;
		bool valid = false;

		const AnmBoneTrack* FindTrack(uint32_t hash) const {
			for (auto& t : tracks)
				if (t.boneNameHash == hash) return &t;
			return nullptr;
		}
	};

	// FNV-1 hash used by Riot for bone/submesh names (lowercase)
	inline uint32_t FNV1Hash(const std::string& str) {
		uint32_t hash = 0x811c9dc5u;
		for (char c : str) {
			char lower = (char)tolower((unsigned char)c);
			hash = (hash ^ (uint32_t)lower) * 0x01000193u;
		}
		return hash;
	}

	// Elf hash used by .skl "Version2" bone name hashing and by legacy .anm joint names.
	inline uint32_t ElfHash(const std::string& str) {
		uint32_t hash = 0;
		for (char c : str) {
			char lower = (char)tolower((unsigned char)c);
			hash = (hash << 4) + (uint32_t)lower;
			uint32_t high = hash & 0xF0000000u;
			if (high != 0) {
				hash ^= high >> 24;
				hash ^= high;
			}
		}
		return hash;
	}
}
