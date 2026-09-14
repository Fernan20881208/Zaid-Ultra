#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "core/Telemetry.hpp"
#include "core/RunPredictor.hpp"
#include "core/SmartDetail.hpp"
#include "core/SmartDetailRenderer.hpp"
#include "platform/AndroidGuardian.hpp"
#include "ui/ZaidUltraPopup.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace zaidultra;

namespace {

float targetFps() {
    auto director = CCDirector::get();
    if (!director) {
        return 60.f;
    }

    const double interval = director->getAnimationInterval();
    if (!std::isfinite(interval) || interval <= 0.0) {
        return 60.f;
    }

    return std::clamp(static_cast<float>(std::round(1.0 / interval)), 30.f, 1000.f);
}

bool coreEnabled() {
    return Mod::get()->getSettingValue<bool>("core-enabled");
}

} // namespace

class $modify(ZaidUltraMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto menu = this->getChildByID("bottom-menu");
        if (!menu) return true;

        auto sprite = ButtonSprite::create("ZU");
        sprite->setScale(.55f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(ZaidUltraMenuLayer::onZaidUltra)
        );
        button->setID("main-button"_spr);
        menu->addChild(button);
        menu->updateLayout();
        return true;
    }

    void onZaidUltra(CCObject*) {
        ZaidUltraPopup::create(UltraTab::Dashboard)->show();
    }
};

class $modify(ZaidUltraGarageLayer, GJGarageLayer) {
    bool init() {
        if (!GJGarageLayer::init()) return false;

        auto menu = CCMenu::create();
        menu->setID("icon-fx-menu"_spr);
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 100);

        auto sprite = ButtonSprite::create("FX");
        sprite->setScale(.55f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(ZaidUltraGarageLayer::onIconFX)
        );
        button->setID("icon-fx-button"_spr);

        auto win = CCDirector::get()->getWinSize();
        button->setPosition({win.width - 38.f, win.height - 32.f});
        menu->addChild(button);
        return true;
    }

    void onIconFX(CCObject*) {
        ZaidUltraPopup::create(UltraTab::IconFX)->show();
    }
};

class $modify(ZaidUltraPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        if (coreEnabled()) {
            Telemetry::get().resetLevel();
            const float savedBest = level ? static_cast<float>(level->m_normalPercent.value()) : 0.f;
            RunPredictor::get().resetLevel(savedBest);
            RunPredictor::get().startAttempt();
            SmartDetail::get().resetSession();
            SmartDetailRenderer::get().reset();
            AndroidGuardian::get().reset();
        }
        return true;
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        if (coreEnabled()) {
            SmartDetailRenderer::get().scan(this);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (!coreEnabled()) return;

        Telemetry::get().resetAttempt();
        if (Mod::get()->getSettingValue<bool>("run-predictor")) {
            RunPredictor::get().startAttempt();
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!coreEnabled()) return;

        const float percent = this->getCurrentPercent();
        const float fpsTarget = targetFps();

        // Telemetry is shared. Smart Detail and Auto Benchmark need the same
        // samples even if the user hides the Frame Doctor UI.
        if (Mod::get()->getSettingValue<bool>("frame-doctor") ||
            Mod::get()->getSettingValue<bool>("auto-benchmark") ||
            Mod::get()->getSettingValue<bool>("smart-detail")) {
            Telemetry::get().sample(dt, percent);
        }

        if (Mod::get()->getSettingValue<bool>("android-guardian")) {
            AndroidGuardian::get().tick(dt);
        }

        if (Mod::get()->getSettingValue<bool>("run-predictor")) {
            RunPredictor::get().observeProgress(percent);
        }

        if (Mod::get()->getSettingValue<bool>("smart-detail")) {
            SmartDetail::get().observe(percent, dt * 1000.f, fpsTarget);
            SmartDetailRenderer::get().apply(this, SmartDetail::get().activeTier());
        } else {
            // If Smart Detail is disabled mid-level, restore normal rendering.
            SmartDetailRenderer::get().apply(this, DetailTier::Normal);
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (coreEnabled() && Mod::get()->getSettingValue<bool>("run-predictor")) {
            RunPredictor::get().recordDeath(this->getCurrentPercent());
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (coreEnabled() && Mod::get()->getSettingValue<bool>("run-predictor")) {
            RunPredictor::get().recordCompletion();
        }
        PlayLayer::levelComplete();
    }
};
