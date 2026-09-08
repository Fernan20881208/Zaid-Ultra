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

#ifdef GEODE_IS_ANDROID
        // Geometry Dash 2.2 already has timestamp-aware queued input support.
        // Explicitly enable the native Click Between Steps path unless a
        // dedicated CBF implementation is installed, in which case it owns
        // the input pipeline and Zaid-Ultra deliberately stays out of its way.
        if (Mod::get()->getSettingValue<bool>("native-subframe-input")) {
            const bool externalCBF =
                Loader::get()->getLoadedMod("syzzi.click_between_frames") != nullptr ||
                Loader::get()->getLoadedMod("zmx.cbf-lite") != nullptr;

            if (!externalCBF) {
                this->m_clickBetweenSteps = true;
                this->m_clickOnSteps = false;
                if (Mod::get()->getSettingValue<bool>("diagnostic-logs")) {
                    log::info("Native Click Between Steps enabled");
                }
            }
            else if (Mod::get()->getSettingValue<bool>("diagnostic-logs")) {
                log::info("External CBF detected; native sub-frame override skipped");
            }
        }
#endif

        // PlayLayer::init executes on the gameplay/render thread on Android,
        // making this the right place for per-thread latency tuning.
        zaid::ultra::LatencyManager::get().onGameplayThread();
        return true;
    }
};
