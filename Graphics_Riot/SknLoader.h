#pragma once
#include "RiotFormats.h"

namespace Riot {

    class SknLoader {
    public:
        // Parses a .skn file loaded fully into memory.
        static SknModel Load(const std::string& filePath);
    };
}
