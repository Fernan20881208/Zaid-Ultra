#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>

#include "core/SessionManager.hpp"
#include "core/Settings.hpp"
#include "modules/InstantReplay.hpp"
#include "modules/TelemetryManager.hpp"
#include "ui/ReplayFloatingControl.hpp"
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
bool g_androidInputListenerInstalled = false;
#endif

void showRootConsole() {
    if (auto* popup = zaid::ultra::RootConsolePopup::create()) {
        popup->show();
    }
}

CCMenuItemSpriteExtra* makeConsoleButton(CCObject* target, SEL_MenuHandler selector) {
    auto* sprite = ButtonSprite::create("ZU", "bigFont.fnt", "GJ_button_05.png", 0.72f);
    sprite->setScale(0.62f);
    auto* button = CCMenuItemSpriteExtra::create(sprite, target, selector);
    button->setID("zaid-ultra-console-button");
    return button;
}

CCMenuItemSpriteExtra* makeReplayButton(CCObject* target, SEL_MenuHandler selector) {
    auto* sprite = ButtonSprite::create("Clip", "bigFont.fnt", "GJ_button_01.png", 0.62f);
    sprite->setScale(0.54f);
    auto* button = CCMenuItemSpriteExtra::create(sprite, target, selector);
    button->setID("zaid-ultra-save-replay-button");
    return button;
}

void addUtilityButtons(
    CCNode* parent,
    CCMenu* layoutMenu,
    CCObject* target,
    SEL_MenuHandler consoleSelector,
    SEL_MenuHandler replaySelector
) {
    if (!parent) {
        return;
    }

    if (layoutMenu) {
        // Node IDs supplies aspect-ratio-safe side menus with layouts. Reusing
        // them also lets other pause-menu mods reposition this button cleanly.
        if (zaid::ultra::settings::enabled("pause-console-button")) {
            layoutMenu->addChild(makeConsoleButton(target, consoleSelector));
        }
        // Keep the button visible even while replay is disabled so the user
        // gets a clear in-game prompt to enable it instead of thinking it is
        // missing from the build.
        layoutMenu->addChild(makeReplayButton(target, replaySelector));
        layoutMenu->updateLayout();
        return;
    }

    // Standalone fallback when Node IDs is unavailable. A small, explicitly
    // anchored menu avoids CCMenu's default full-screen anchor transform,
    // which could place the old absolute-position button outside the screen.
    auto size = parent->getContentSize();
    if (size.width < 100.0f || size.height < 100.0f) {
        size = CCDirector::sharedDirector()->getWinSize();
    }
    auto* menu = CCMenu::create();
    menu->setContentSize({92.0f, 44.0f});
    menu->setAnchorPoint({0.5f, 0.5f});
    menu->ignoreAnchorPointForPosition(false);
    menu->setPosition({56.0f, size.height - 32.0f});
    menu->setID("zaid-ultra-console-menu");
    if (zaid::ultra::settings::enabled("pause-console-button")) {
        auto* console = makeConsoleButton(target, consoleSelector);
        console->setPosition({22.0f, 22.0f});
        menu->addChild(console);
    }
    auto* replay = makeReplayButton(target, replaySelector);
    replay->setPosition({70.0f, 22.0f});
    menu->addChild(replay);
    parent->addChild(menu, 1000);
}

bool externalCbfLoaded() {
    return Loader::get()->getLoadedMod("syzzi.click_between_frames") != nullptr ||
        Loader::get()->getLoadedMod("zmx.cbf-lite") != nullptr;
}

#ifdef GEODE_IS_ANDROID
void ensureAndroidInputListener() {
    if (g_androidInputListenerInstalled) {
        return;
    }

    // Register only after Geometry Dash has created a PlayLayer. Keeping this
    // out of the mod-loading phase avoids touching Android event plumbing
    // while Geode is still loading and enabling other mods.
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
    g_androidInputListenerInstalled = true;
}
#endif

} // namespace

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
        ensureAndroidInputListener();
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
        if (zaid::ultra::settings::enabled("instant-replay") &&
            zaid::ultra::settings::enabled("replay-floating-control")) {
            if (auto* replayControl = zaid::ultra::ReplayFloatingControl::create()) {
                replayControl->setID("zaid-ultra-floating-replay-control");
                if (this->m_uiLayer) {
                    this->m_uiLayer->addChild(replayControl, 5000);
                } else {
                    this->addChild(replayControl, 5000);
                }
            }
        }
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
        auto* sideMenu = typeinfo_cast<CCMenu*>(this->getChildByID("left-button-menu"));
        addUtilityButtons(
            this,
            sideMenu,
            this,
            menu_selector(ZaidUltraPauseLayer::onZaidUltraConsole),
            menu_selector(ZaidUltraPauseLayer::onZaidUltraReplay)
        );
    }

    void onZaidUltraConsole(CCObject*) {
        showRootConsole();
    }

    void onZaidUltraReplay(CCObject*) {
        if (zaid::ultra::InstantReplay::get().saveLast60Seconds()) {
            Notification::create("Guardando clip retroactivo...", NotificationIcon::Info)->show();
        }
    }
};

class $modify(ZaidUltraEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        auto* sideMenu = typeinfo_cast<CCMenu*>(this->getChildByID("hide-layer-menu"));
        addUtilityButtons(
            this,
            sideMenu,
            this,
            menu_selector(ZaidUltraEndLevelLayer::onZaidUltraConsole),
            menu_selector(ZaidUltraEndLevelLayer::onZaidUltraReplay)
        );
    }

    void onZaidUltraConsole(CCObject*) {
        showRootConsole();
    }

    void onZaidUltraReplay(CCObject*) {
        if (zaid::ultra::InstantReplay::get().saveLast60Seconds()) {
            Notification::create("Guardando clip retroactivo...", NotificationIcon::Info)->show();
        }
    }
};
