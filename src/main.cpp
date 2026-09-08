#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "LatencyManager.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    zaid::ultra::LatencyManager::get().prime();
}

class $modify(ZaidUltraPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        // PlayLayer::init executes on the gameplay/render thread on Android,
        // making this the right place for per-thread latency tuning.
        zaid::ultra::LatencyManager::get().onGameplayThread();
        return true;
    }
};
