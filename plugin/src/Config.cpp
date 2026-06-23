#include "Config.hpp"
#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
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
    g_defaultPresetOrient = std::clamp(ReadFloat("Defaults", "PresetOrient", 0.5f), 0.0f, 1.0f);
    g_defaultFantasyRatio = std::clamp(ReadFloat("Defaults", "FantasyRatio", 0.15f), 0.0f, 1.0f);
    g_defaultUnusualRatio       = std::clamp(ReadFloat("Defaults", "UnusualRatio", 0.06f), 0.0f, 1.0f);
    g_defaultBreastUnusualRatio = std::clamp(ReadFloat("Defaults", "BreastUnusualRatio", 0.06f), 0.0f, 1.0f);
    g_defaultAthleticRatio      = std::clamp(ReadFloat("Defaults", "AthleticRatio", 0.15f), 0.0f, 1.0f);
    g_defaultReRollKey          = static_cast<int>(ReadFloat("Defaults", "ReRollKey", 26.0f));
    g_defaultFemaleBodies       = GetPrivateProfileIntA("Defaults", "FemaleBodies", 1, kIniPath) != 0;
    g_defaultMaleBodies         = GetPrivateProfileIntA("Defaults", "MaleBodies", 1, kIniPath) != 0;
    g_defaultMaleBuild          = std::clamp(ReadFloat("Defaults", "MaleBuild", 1.0f), 0.0f, 2.0f);
    g_defaultDebugLog           = GetPrivateProfileIntA("Defaults", "DebugLog", 0, kIniPath) != 0;
    g_defaultNeckColorFix       = std::clamp(ReadFloat("Defaults", "NeckColorFix", 0.5f), 0.0f, 1.0f);
    SKSE::log::info("Config: MorphScale={:.2f}, Fantasy={:.2f}, Unusual={:.2f}, BreastUnusual={:.2f}, Athletic={:.2f}, FemaleBodies={}, MaleBodies={}, MaleBuild={:.2f}",
                    g_defaultMorphScale, g_defaultFantasyRatio, g_defaultUnusualRatio,
                    g_defaultBreastUnusualRatio, g_defaultAthleticRatio, g_defaultFemaleBodies, g_defaultMaleBodies, g_defaultMaleBuild);
}

namespace {

constexpr const char* kExclusionDir  = R"(.\Data\SKSE\Plugins)";
constexpr const char* kMcmExclFile   = R"(.\Data\SKSE\Plugins\OBodyNGWeight_Exclusions_MCM.txt)";

std::string LowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void ReadExclusionFile(const std::filesystem::path& a_path, std::unordered_set<std::string>& a_out) {
    std::ifstream f(a_path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t b = 0, e = line.size();
        while (b < e && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(line[e - 1]))) --e;
        if (e <= b || line[b] == ';' || line[b] == '#') continue;
        a_out.insert(LowerStr(line.substr(b, e - b)));
    }
}

void WriteMcmExclusionFile() {
    std::ofstream f(kMcmExclFile, std::ios::trunc);
    if (!f) return;
    f << "; OBodyNG Weight - MCM-managed plugin exclusions (auto-generated; toggle these in the MCM).\n";
    for (const auto& n : g_mcmExcluded) f << n << "\n";
}

}  // namespace

void LoadExclusions() {
    g_mcmExcluded.clear();
    g_fileExcluded.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(fs::path(kExclusionDir), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        // Match OBodyNGWeight_Exclusions*.txt via the WIDE filename (no codepage conversion, so a
        // non-ASCII file elsewhere in the folder can't throw).
        std::wstring fn = it->path().filename().native();
        for (auto& c : fn) c = static_cast<wchar_t>(std::towlower(c));
        if (fn.rfind(L"obodyngweight_exclusions", 0) != 0) continue;
        if (fn.size() < 4 || fn.compare(fn.size() - 4, 4, L".txt") != 0) continue;
        if (fn == L"obodyngweight_exclusions_mcm.txt") ReadExclusionFile(it->path(), g_mcmExcluded);
        else                                           ReadExclusionFile(it->path(), g_fileExcluded);
    }
    SKSE::log::info("Config: exclusions - {} from MCM, {} from files", g_mcmExcluded.size(), g_fileExcluded.size());
}

bool IsActorExcluded(RE::Actor* a_actor) {
    if ((g_mcmExcluded.empty() && g_fileExcluded.empty()) || !a_actor) return false;
    const auto fromExcluded = [](RE::TESForm* a_form) -> bool {
        if (!a_form) return false;
        for (std::int32_t idx : { 0, -1 }) {  // 0 = origin (added by), -1 = winning override
            if (auto* file = a_form->GetFile(idx)) {
                const std::string n = LowerStr(std::string(file->GetFilename()));
                if (g_mcmExcluded.count(n) || g_fileExcluded.count(n)) return true;
            }
        }
        return false;
    };
    return fromExcluded(a_actor->GetActorBase()) || fromExcluded(a_actor);
}

bool IsPluginExcluded(const char* a_plugin) {
    return a_plugin && g_mcmExcluded.count(LowerStr(a_plugin)) > 0;
}

void SetPluginExcluded(const char* a_plugin, bool a_on) {
    if (!a_plugin) return;
    const std::string n = LowerStr(a_plugin);
    if (n.empty()) return;
    if (a_on) g_mcmExcluded.insert(n);
    else      g_mcmExcluded.erase(n);
    WriteMcmExclusionFile();
}

std::vector<std::string> GetNpcPlugins() {
    std::set<std::string> names;  // sorted, unique, display case
    if (auto* dh = RE::TESDataHandler::GetSingleton()) {
        for (auto* npc : dh->GetFormArray<RE::TESNPC>()) {
            if (!npc) continue;
            if (auto* file = npc->GetFile(0))  // origin plugin (the one that added the NPC)
                names.emplace(file->GetFilename());
        }
    }
    // Full list (the MCM Exclusions page paginates it ~120/page; GetNpcPluginsPage chunks this).
    return std::vector<std::string>(names.begin(), names.end());
}

}  // namespace OBW::Config
