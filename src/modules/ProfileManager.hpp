#pragma once

#include <Geode/Geode.hpp>

#include <mutex>
#include <string>

namespace zaid::ultra {

struct GameplayProfile final {
    int levelId = 0;
    bool extremeDemon = false;
    bool manual = false;
    bool ultra = false;
    bool monitorOnly = false;
    bool request120Hz = false;
    bool touchBoost = false;
    bool suppressHeadsUp = false;
    bool memoryTrim = false;
    bool thermalMonitor = false;
    std::string name = "normal";
};

class ProfileManager final {
public:
    static ProfileManager& get();

    GameplayProfile resolve(GJGameLevel* level);
    GameplayProfile current() const;
    void clear();

private:
    ProfileManager() = default;

    static std::string manualProfileFor(int levelId);

    mutable std::mutex m_mutex;
    GameplayProfile m_current;
};

} // namespace zaid::ultra
