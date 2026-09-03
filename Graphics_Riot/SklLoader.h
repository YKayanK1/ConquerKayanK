#pragma once
#include "RiotFormats.h"

namespace Riot {

    class SklLoader {
    public:
        static SklModel Load(const std::string& filePath);
    };
}
