#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/System.hpp>
#include <Geode/binding/ButtonSprite.hpp>

#include "core/SessionManager.hpp"
#include "core/Settings.hpp"
#include "modules/AudioLatency.hpp"
#include "modules/ReplayProbe.hpp"
#include "modules/TelemetryManager.hpp"
#include "ui/RootConsolePopup.hpp"
#include "ui/TelemetryOverlay.hpp"

#ifdef GEODE_IS_ANDROID
#include <Geode/utils/AndroidEvent.hpp>
#include <variant>
#endif

using namespace geode::prelude;

namespace {

#ifdef GEODE_IS_ANDROID
ListenerHandle g_androidInputListener;
#endif

void showRootConsole() {
    if (auto* popup = zaid::ultra::RootConsolePopup::create()) {
        popup->show();
    }
}

void addConsoleButton(CCNode* parent, CCObject* target, SEL_MenuHandler selector, CCPoint position) {
    if (!parent || !zaid::ultra::settings::enabled("pause-console-button")) {
        return;
    }
    auto* menu = CCMenu::create();
    menu->setPosition({0.0f, 0.0f});
    menu->setID("zaid-ultra-console-menu");
    auto* sprite = ButtonSprite::create("ZU", "bigFont.fnt", "GJ_button_05.png", 0.72f);
    sprite->setScale(0.62f);
    auto* button = CCMenuItemSpriteExtra::create(sprite, target, selector);
    button->setID("zaid-ultra-console-button");
    button->setPosition(position);
    menu->addChild(button);
    parent->addChild(menu, 1000);
}

bool externalCbfLoaded() {
    return Loader::get()->getLoadedMod("syzzi.click_between_frames") != nullptr ||
        Loader::get()->getLoadedMod("zmx.cbf-lite") != nullptr;
}

} // namespace

$on_mod(Loaded) {
    zaid::ultra::SessionManager::get().prime();

#ifdef GEODE_IS_ANDROID
    // Observe MotionEvent timestamps at Geode's raw Android boundary. Returning
    // Propagate is essential: CBF and the game's normal input path still own
    // delivery and physics behavior.
    g_androidInputListener = AndroidRichInputEvent().listen(
        [](std::int64_t timestamp, int, int, AndroidRichInput input) {
            if (auto* touch = std::get_if<AndroidTouchInput>(&input)) {
                zaid::ultra::TelemetryManager::get().onTouch(
                    timestamp,
                    static_cast<int>(touch->type())
                );
            }
            return ListenerResult::Propagate;
        },
        Priority::VeryEarly
    );
#endif
}

class $modify(ZaidUltraFMODSystem, FMOD::System) {
    FMOD_RESULT init(int maxChannels, FMOD_INITFLAGS flags, void* extraData) {
        zaid::ultra::AudioLatency::get().beforeSystemInit(this);
        auto result = FMOD::System::init(maxChannels, flags, extraData);
        zaid::ultra::AudioLatency::get().afterSystemInit(this, result);
        return result;
    }
};

class $modify(ZaidUltraScheduler, CCScheduler) {
    void update(float dt) {
        zaid::ultra::TelemetryManager::get().onFrame();
        CCScheduler::update(dt);
    }
};

class $modify(ZaidUltraBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        // A read-only marker at the engine's command/physics boundary. CBF may
        // split player updates around the same Android timestamp; this hook
        // does not consume queues, modify dt or call handleButton.
        zaid::ultra::TelemetryManager::get().onPhysicsBoundary();
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(ZaidUltraPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto profile = zaid::ultra::SessionManager::get().prepare(level);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            zaid::ultra::SessionManager::get().cancelPrepared();
            return false;
        }

#ifdef GEODE_IS_ANDROID
        if (zaid::ultra::settings::enabled("native-subframe-input")) {
            if (!externalCbfLoaded()) {
                this->m_clickBetweenSteps = true;
                this->m_clickOnSteps = false;
                if (zaid::ultra::settings::diagnostics()) {
                    log::info("Native Click Between Steps enabled");
                }
            } else if (zaid::ultra::settings::diagnostics()) {
                log::info("External CBF detected; Zaid-Ultra remains metrics-only for input/physics");
            }
        }
#endif

        zaid::ultra::SessionManager::get().begin();
        if (zaid::ultra::settings::enabled("telemetry-overlay")) {
            if (auto* overlay = zaid::ultra::TelemetryOverlay::create()) {
                overlay->setID("zaid-ultra-telemetry-overlay");
                if (this->m_uiLayer) {
                    this->m_uiLayer->addChild(overlay, 1000);
                } else {
                    this->addChild(overlay, 1000);
                }
            }
        }

        if (zaid::ultra::settings::diagnostics()) {
            log::info("PlayLayer active with profile '{}'", profile.name);
        }
        return true;
    }

    void onExit() {
        zaid::ultra::SessionManager::get().end();
        PlayLayer::onExit();
    }
};

class $modify(ZaidUltraPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto size = CCDirector::sharedDirector()->getWinSize();
        addConsoleButton(
            this,
            this,
            menu_selector(ZaidUltraPauseLayer::onZaidUltraConsole),
            {size.width - 30.0f, size.height - 28.0f}
        );
    }

    void onZaidUltraConsole(CCObject*) {
        showRootConsole();
    }
};

class $modify(ZaidUltraEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        auto size = CCDirector::sharedDirector()->getWinSize();
        addConsoleButton(
            this,
            this,
            menu_selector(ZaidUltraEndLevelLayer::onZaidUltraConsole),
            {size.width - 30.0f, size.height - 28.0f}
        );
    }

    void onZaidUltraConsole(CCObject*) {
        showRootConsole();
    }
};
