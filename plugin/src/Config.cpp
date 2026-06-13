#include "Config.hpp"
#include <SKSE/SKSE.h>
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <string>

namespace OBW::Config {

namespace {

constexpr const char* kIniPath = R"(.\Data\SKSE\Plugins\OBodyNGWeight.ini)";

float ReadFloat(const char* section, const char* key, float fallback) {
    char buf[64]{};
    const DWORD n = GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), kIniPath);
    if (n == 0) return fallback;
    try {
        return std::stof(std::string(buf, n));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

void Load() {
    g_defaultMorphScale   = std::clamp(ReadFloat("Defaults", "MorphScale", 1.0f), 0.0f, 2.5f);
    g_defaultFantasyRatio = std::clamp(ReadFloat("Defaults", "FantasyRatio", 0.15f), 0.0f, 1.0f);
    g_defaultUnusualRatio       = std::clamp(ReadFloat("Defaults", "UnusualRatio", 0.06f), 0.0f, 1.0f);
    g_defaultBreastUnusualRatio = std::clamp(ReadFloat("Defaults", "BreastUnusualRatio", 0.06f), 0.0f, 1.0f);
    g_defaultAthleticRatio      = std::clamp(ReadFloat("Defaults", "AthleticRatio", 0.15f), 0.0f, 1.0f);
    g_defaultReRollKey          = static_cast<int>(ReadFloat("Defaults", "ReRollKey", 26.0f));
    SKSE::log::info("Config: MorphScale={:.2f}, Fantasy={:.2f}, Unusual={:.2f}, BreastUnusual={:.2f}, Athletic={:.2f}",
                    g_defaultMorphScale, g_defaultFantasyRatio, g_defaultUnusualRatio,
                    g_defaultBreastUnusualRatio, g_defaultAthleticRatio);
}

}  // namespace OBW::Config
