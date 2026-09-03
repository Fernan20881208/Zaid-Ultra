#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "core/Telemetry.hpp"
#include "core/RunPredictor.hpp"
#include "core/SmartDetail.hpp"
#include "ui/ZaidUltraPopup.hpp"

using namespace geode::prelude;
using namespace zaidultra;

namespace {

float targetFps() {
    // v0.1 foundation target. Android Guardian will replace this with the
    // active display/game target in the platform adapter.
    return 120.f;
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
        if (Mod::get()->getSettingValue<bool>("frame-doctor")) {
            Telemetry::get().sample(dt, percent);
        }
        if (Mod::get()->getSettingValue<bool>("run-predictor")) {
            RunPredictor::get().observeProgress(percent);
        }
        if (Mod::get()->getSettingValue<bool>("smart-detail")) {
            SmartDetail::get().observe(percent, dt * 1000.f, targetFps());

            // First safe adaptive renderer: shed non-essential built-in glitter
            // before more aggressive object culling. Gameplay/collision objects
            // are never removed or mutated.
            const bool fullQuality = SmartDetail::get().activeTier() == DetailTier::Normal;
            this->toggleGlitter(fullQuality);
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
