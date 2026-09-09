#pragma once

#include "../modules/ProfileManager.hpp"

#include <mutex>
#include <string>

class GJGameLevel;

namespace zaid::ultra {

class SessionManager final {
public:
    static SessionManager& get();

    void prime();
    GameplayProfile prepare(GJGameLevel* level);
    void begin();
    void end();
    void cancelPrepared();

    bool active() const;
    std::string statusLine() const;

private:
    SessionManager() = default;

    mutable std::mutex m_mutex;
    bool m_active = false;
    GameplayProfile m_profile;
};

} // namespace zaid::ultra
