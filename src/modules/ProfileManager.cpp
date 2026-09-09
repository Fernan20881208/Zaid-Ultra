#include "ProfileManager.hpp"

#include "../core/Settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace zaid::ultra {
namespace {

std::string trim(std::string value) {
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

ProfileManager& ProfileManager::get() {
    static ProfileManager instance;
    return instance;
}

GameplayProfile ProfileManager::resolve(GJGameLevel* level) {
    GameplayProfile profile;
    if (level) {
        profile.levelId = level->m_levelID.value();
        profile.extremeDemon = level->m_demon.value() != 0 &&
            level->m_demonDifficulty == static_cast<int>(DemonDifficultyType::ExtremeDemon);
    }

    auto manual = manualProfileFor(profile.levelId);
    profile.manual = !manual.empty();
    profile.monitorOnly = manual == "monitor";
    profile.ultra = manual == "ultra" ||
        (profile.extremeDemon && settings::enabled("extreme-demon-profile"));

    if (profile.monitorOnly) {
        profile.name = "manual:monitor";
    } else if (profile.manual && profile.ultra) {
        profile.name = "manual:ultra";
    } else if (profile.ultra && profile.extremeDemon) {
        profile.name = "extreme-demon:ultra";
    } else {
        profile.name = "normal";
    }

    profile.request120Hz = !profile.monitorOnly &&
        (settings::enabled("dynamic-120hz") || profile.ultra);
    profile.touchBoost = !profile.monitorOnly &&
        (settings::enabled("root-touch-boost") || profile.ultra);
    profile.suppressHeadsUp = !profile.monitorOnly &&
        (settings::enabled("notification-guard") || profile.ultra);
    profile.memoryTrim = !profile.monitorOnly &&
        (settings::enabled("prudent-memory-trim") || profile.ultra);
    profile.thermalMonitor = settings::enabled("thermal-monitor") || profile.monitorOnly || profile.ultra;

    {
        std::lock_guard lock(m_mutex);
        m_current = profile;
    }
    return profile;
}

GameplayProfile ProfileManager::current() const {
    std::lock_guard lock(m_mutex);
    return m_current;
}

void ProfileManager::clear() {
    std::lock_guard lock(m_mutex);
    m_current = {};
}

std::string ProfileManager::manualProfileFor(int levelId) {
    if (levelId <= 0) {
        return {};
    }
    auto raw = settings::text("manual-level-profiles");
    std::replace(raw.begin(), raw.end(), ';', ',');
    std::stringstream stream(raw);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }
        auto separator = token.find(':');
        auto idText = trim(token.substr(0, separator));
        char* end = nullptr;
        auto parsed = std::strtol(idText.c_str(), &end, 10);
        if (end == idText.c_str() || *end != '\0' || parsed != levelId) {
            continue;
        }
        auto mode = separator == std::string::npos
            ? std::string("ultra")
            : lower(trim(token.substr(separator + 1)));
        if (mode == "ultra" || mode == "monitor") {
            return mode;
        }
    }
    return {};
}

} // namespace zaid::ultra
