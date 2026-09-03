#pragma once
#include "RiotFormats.h"

namespace Riot {

    class AnmLoader {
    public:
        static AnmModel Load(const std::string& filePath);
    };
}
