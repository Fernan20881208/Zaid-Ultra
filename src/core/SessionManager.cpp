#include "SessionManager.hpp"

#include "../LatencyManager.hpp"
#include "RootExecutor.hpp"
#include "RootStateGuard.hpp"
#include "Settings.hpp"
#include "../modules/AudioLatency.hpp"
#include "../modules/ForcedTouchBoost.hpp"
#include "../modules/InstantReplay.hpp"
#include "../modules/MemoryTrim.hpp"
#include "../modules/TelemetryManager.hpp"
#include "../platform/AndroidBridge.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace zaid::ultra {

SessionManager& SessionManager::get() {
    static SessionManager instance;
    return instance;
}

void SessionManager::prime() {
    std::call_once(m_primeOnce, [] {
        // None of this runs while Geode is still loading mods. MenuLayer
        // starts it once the game is ready; PlayLayer::prepare remains a
        // fallback for launch flows that bypass the normal main menu.
        LatencyManager::get().prime();
        RootExecutor::get().start();
        // Recovery is deliberately queued first: no new persistent value may
        // be changed until an interrupted session has been restored.
        RootStateGuard::get().recoverIfNeeded();
        // The two device-validated touch switches are mandatory and owned for
        // the whole game lifetime, independently from per-level profiles.
        ForcedTouchBoost::get().start();
        RootExecutor::get().probe();
        RootStateGuard::get().refreshReadOnlyStatus();
        AudioLatency::get().refreshDiagnostics();
    });
}

GameplayProfile SessionManager::prepare(GJGameLevel* level) {
    prime();
    end();
    auto profile = ProfileManager::get().resolve(level);
    MemoryTrim::get().beforeGameplay(profile.memoryTrim);
    {
        std::lock_guard lock(m_mutex);
        m_profile = profile;
    }
    if (settings::diagnostics()) {
        log::info(
            "Gameplay profile prepared: '{}' level={} extreme={} manual={}",
            profile.name,
            profile.levelId,
            profile.extremeDemon,
            profile.manual
        );
    }
    return profile;
}

void SessionManager::begin() {
    GameplayProfile profile;
    {
        std::lock_guard lock(m_mutex);
        if (m_active) {
            return;
        }
        profile = m_profile;
        m_active = true;
    }

    LatencyManager::get().begin();
    if (profile.request120Hz) {
        AndroidBridge::get().requestRefreshRate(120.0f);
    }

    RootGuardConfig rootConfig;
    // ForcedTouchBoost owns these nodes globally. Keeping them out of the
    // PlayLayer guard prevents a level exit from restoring 240 Hz.
    rootConfig.touchBoost = false;
    rootConfig.refreshRate = profile.request120Hz && settings::enabled("refresh-root-fallback");
    rootConfig.suppressHeadsUp = profile.suppressHeadsUp;
    if (rootConfig.touchBoost || rootConfig.refreshRate || rootConfig.suppressHeadsUp) {
        RootStateGuard::get().begin(rootConfig);
    }

    AudioLatency::get().refreshDiagnostics();
    TelemetryManager::get().start(profile.thermalMonitor);
    InstantReplay::get().beginGameplay();
}

void SessionManager::end() {
    bool wasActive = false;
    {
        std::lock_guard lock(m_mutex);
        wasActive = m_active;
        m_active = false;
    }
    if (!wasActive) {
        return;
    }

    InstantReplay::get().endGameplay();
    TelemetryManager::get().stop();
    AndroidBridge::get().restoreRefreshRate();
    RootStateGuard::get().end();
    LatencyManager::get().end();
    ProfileManager::get().clear();

    if (settings::diagnostics()) {
        log::info("Gameplay session ended; reversible state restoration queued/completed");
    }
}

void SessionManager::cancelPrepared() {
    ProfileManager::get().clear();
    std::lock_guard lock(m_mutex);
    m_profile = {};
}

bool SessionManager::active() const {
    std::lock_guard lock(m_mutex);
    return m_active;
}

std::string SessionManager::statusLine() const {
    std::lock_guard lock(m_mutex);
    return fmt::format(
        "{} | nivel={} | perfil={}",
        m_active ? "activo" : "inactivo",
        m_profile.levelId,
        m_profile.name
    );
}

} // namespace zaid::ultra
