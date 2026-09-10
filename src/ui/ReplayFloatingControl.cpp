#include "ReplayFloatingControl.hpp"

#include "../modules/InstantReplay.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace zaid::ultra {
namespace {

constexpr float kToggleHalfWidth = 27.0f;
constexpr float kToggleHalfHeight = 23.0f;
constexpr float kPanelWidth = 106.0f;
constexpr float kPanelHeight = 132.0f;

CCMenuItemSpriteExtra* actionButton(
    char const* label,
    CCObject* target,
    SEL_MenuHandler selector,
    char const* background
) {
    auto* sprite = ButtonSprite::create(label, "bigFont.fnt", background, 0.58f);
    sprite->setScale(0.46f);
    return CCMenuItemSpriteExtra::create(sprite, target, selector);
}

void setButtonEnabled(CCMenuItemSpriteExtra* button, bool enabled) {
    if (!button) {
        return;
    }
    button->setEnabled(enabled);
    button->setOpacity(enabled ? 255 : 105);
}

} // namespace

ReplayFloatingControl* ReplayFloatingControl::create() {
    auto* result = new ReplayFloatingControl();
    if (result && result->init()) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayFloatingControl::init() {
    if (!CCLayer::init()) {
        return false;
    }

    auto size = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(size);
    this->setAnchorPoint({0.0f, 0.0f});
    this->ignoreAnchorPointForPosition(true);

    m_panelBackground = CCLayerColor::create(ccc4(0, 0, 0, 178), kPanelWidth, kPanelHeight);
    m_panelBackground->setAnchorPoint({0.5f, 0.5f});
    m_panelBackground->ignoreAnchorPointForPosition(false);
    this->addChild(m_panelBackground, 0);

    m_panelTitle = CCLabelBMFont::create("REPLAY MANUAL", "chatFont.fnt");
    m_panelTitle->setScale(0.34f);
    m_panelTitle->setColor(ccc3(255, 220, 80));
    this->addChild(m_panelTitle, 2);

    m_actionMenu = CCMenu::create();
    m_actionMenu->setAnchorPoint({0.0f, 0.0f});
    m_actionMenu->ignoreAnchorPointForPosition(true);
    m_actionMenu->setPosition({0.0f, 0.0f});
    m_actionMenu->setContentSize(size);
    m_actionMenu->setID("zaid-ultra-replay-action-menu");
    this->addChild(m_actionMenu, 2);

    m_recordButton = actionButton(
        "Grabar",
        this,
        menu_selector(ReplayFloatingControl::onRecord),
        "GJ_button_01.png"
    );
    m_pauseButton = actionButton(
        "Pausar",
        this,
        menu_selector(ReplayFloatingControl::onPause),
        "GJ_button_02.png"
    );
    m_resumeButton = actionButton(
        "Seguir",
        this,
        menu_selector(ReplayFloatingControl::onResume),
        "GJ_button_01.png"
    );
    m_finishButton = actionButton(
        "Finalizar",
        this,
        menu_selector(ReplayFloatingControl::onFinish),
        "GJ_button_06.png"
    );
    m_clipButton = actionButton(
        "Clip 60s",
        this,
        menu_selector(ReplayFloatingControl::onSaveClip),
        "GJ_button_04.png"
    );

    m_recordButton->setID("zaid-ultra-replay-record");
    m_pauseButton->setID("zaid-ultra-replay-pause");
    m_resumeButton->setID("zaid-ultra-replay-resume");
    m_finishButton->setID("zaid-ultra-replay-finish");
    m_clipButton->setID("zaid-ultra-replay-save-clip");
    m_actionMenu->addChild(m_recordButton);
    m_actionMenu->addChild(m_pauseButton);
    m_actionMenu->addChild(m_resumeButton);
    m_actionMenu->addChild(m_finishButton);
    m_actionMenu->addChild(m_clipButton);

    auto logoPath = Mod::get()->getResourcesDir() / "replay-button.png";
    m_toggleVisual = CCSprite::create(logoPath.string().c_str());
    if (m_toggleVisual) {
        auto logoSize = m_toggleVisual->getContentSize();
        auto longestSide = std::max(logoSize.width, logoSize.height);
        m_toggleVisual->setScale(longestSide > 0.0f ? 47.0f / longestSide : 1.0f);
    } else {
        auto* fallback = ButtonSprite::create("ZU", "bigFont.fnt", "GJ_button_05.png", 0.72f);
        fallback->setScale(0.58f);
        m_toggleVisual = fallback;
    }
    m_toggleVisual->setID("zaid-ultra-floating-button");
    this->addChild(m_toggleVisual, 4);

    m_stateLabel = CCLabelBMFont::create("LISTO", "chatFont.fnt");
    m_stateLabel->setScale(0.34f);
    m_stateLabel->setOpacity(235);
    this->addChild(m_stateLabel, 5);

    auto savedX = Mod::get()->getSavedValue<double>("replay-floating-x", 0.91);
    auto savedY = Mod::get()->getSavedValue<double>("replay-floating-y", 0.50);
    if (!std::isfinite(savedX) || savedX < 0.0 || savedX > 1.0) {
        savedX = 0.91;
    }
    if (!std::isfinite(savedY) || savedY < 0.0 || savedY > 1.0) {
        savedY = 0.50;
    }
    setFloatingPosition({
        static_cast<float>(savedX * size.width),
        static_cast<float>(savedY * size.height),
    });
    setPanelVisible(false);

    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-1000);
    this->setTouchEnabled(true);
    this->scheduleUpdate();
    refreshState();
    return true;
}

void ReplayFloatingControl::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -1000, true);
}

bool ReplayFloatingControl::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!touch || !m_toggleVisual || !this->isVisible()) {
        return false;
    }
    auto point = this->convertToNodeSpace(touch->getLocation());
    auto hit = CCRect(
        m_floatingPosition.x - kToggleHalfWidth,
        m_floatingPosition.y - kToggleHalfHeight,
        kToggleHalfWidth * 2.0f,
        kToggleHalfHeight * 2.0f
    );
    if (!hit.containsPoint(point)) {
        return false;
    }
    m_trackingTouch = true;
    m_dragging = false;
    m_touchStart = point;
    m_dragOffset = m_floatingPosition - point;
    return true;
}

void ReplayFloatingControl::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_trackingTouch || !touch) {
        return;
    }
    auto point = this->convertToNodeSpace(touch->getLocation());
    auto dx = point.x - m_touchStart.x;
    auto dy = point.y - m_touchStart.y;
    if (!m_dragging && dx * dx + dy * dy >= 36.0f) {
        m_dragging = true;
        setPanelVisible(false);
    }
    if (m_dragging) {
        setFloatingPosition(point + m_dragOffset);
    }
}

void ReplayFloatingControl::ccTouchEnded(CCTouch*, CCEvent*) {
    finishDrag(false);
}

void ReplayFloatingControl::ccTouchCancelled(CCTouch*, CCEvent*) {
    finishDrag(true);
}

void ReplayFloatingControl::finishDrag(bool cancelled) {
    if (!m_trackingTouch) {
        return;
    }
    if (!m_dragging && !cancelled) {
        setPanelVisible(!m_panelVisible);
    } else if (m_dragging) {
        auto size = this->getContentSize();
        if (size.width > 0.0f && size.height > 0.0f) {
            Mod::get()->setSavedValue(
                "replay-floating-x",
                static_cast<double>(m_floatingPosition.x / size.width)
            );
            Mod::get()->setSavedValue(
                "replay-floating-y",
                static_cast<double>(m_floatingPosition.y / size.height)
            );
        }
    }
    m_trackingTouch = false;
    m_dragging = false;
}

void ReplayFloatingControl::setPanelVisible(bool visible) {
    m_panelVisible = visible;
    if (m_panelBackground) {
        m_panelBackground->setVisible(visible);
    }
    if (m_panelTitle) {
        m_panelTitle->setVisible(visible);
    }
    if (m_actionMenu) {
        m_actionMenu->setVisible(visible);
    }
}

void ReplayFloatingControl::setFloatingPosition(CCPoint position) {
    auto size = this->getContentSize();
    position.x = std::clamp(position.x, 30.0f, std::max(30.0f, size.width - 30.0f));
    position.y = std::clamp(position.y, 30.0f, std::max(30.0f, size.height - 30.0f));
    m_floatingPosition = position;

    if (m_toggleVisual) {
        m_toggleVisual->setPosition(position);
    }
    if (m_stateLabel) {
        m_stateLabel->setPosition({position.x, position.y - 27.0f});
    }

    auto opensLeft = position.x >= size.width * 0.5f;
    auto panelX = position.x + (opensLeft ? -87.0f : 87.0f);
    auto panelY = std::clamp(
        position.y,
        kPanelHeight * 0.5f + 4.0f,
        std::max(kPanelHeight * 0.5f + 4.0f, size.height - kPanelHeight * 0.5f - 4.0f)
    );
    panelX = std::clamp(
        panelX,
        kPanelWidth * 0.5f + 4.0f,
        std::max(kPanelWidth * 0.5f + 4.0f, size.width - kPanelWidth * 0.5f - 4.0f)
    );

    if (m_panelBackground) {
        m_panelBackground->setPosition({panelX, panelY});
    }
    if (m_panelTitle) {
        m_panelTitle->setPosition({panelX, panelY + 53.0f});
    }
    if (m_recordButton) {
        m_recordButton->setPosition({panelX, panelY + 30.0f});
    }
    if (m_pauseButton) {
        m_pauseButton->setPosition({panelX, panelY + 8.0f});
    }
    if (m_resumeButton) {
        m_resumeButton->setPosition({panelX, panelY + 8.0f});
    }
    if (m_finishButton) {
        m_finishButton->setPosition({panelX, panelY - 16.0f});
    }
    if (m_clipButton) {
        m_clipButton->setPosition({panelX, panelY - 42.0f});
    }
}

void ReplayFloatingControl::update(float dt) {
    m_elapsed += dt;
    if (m_elapsed < 0.20f) {
        return;
    }
    m_elapsed = 0.0f;
    refreshState();
}

void ReplayFloatingControl::refreshState() {
    auto status = InstantReplay::get().status();
    auto active = status.starting || status.buffering;
    std::string label;
    ccColor3B color = ccc3(210, 210, 210);

    if (!status.enabled) {
        label = "OFF";
        color = ccc3(160, 160, 160);
    } else if (status.saving) {
        label = "GUARDA";
        color = ccc3(100, 220, 255);
    } else if (status.paused) {
        label = "PAUSA";
        color = ccc3(255, 210, 80);
    } else if (status.starting) {
        label = "INICIA";
        color = ccc3(255, 210, 80);
    } else if (status.buffering) {
        label = fmt::format("REC {:.0f}s", std::min(60.0, status.bufferedSeconds));
        color = ccc3(255, 90, 90);
    } else if (!status.lastError.empty()) {
        label = "ERROR";
        color = ccc3(255, 90, 90);
    } else if (status.finalized) {
        label = "FIN";
        color = ccc3(100, 235, 140);
    } else {
        label = "LISTO";
        color = ccc3(100, 235, 140);
    }

    if (m_stateLabel) {
        m_stateLabel->setString(label.c_str());
        m_stateLabel->setColor(color);
    }

    auto canStart = status.enabled && status.gameplayActive && !active &&
        !status.paused && !status.saving;
    setButtonEnabled(m_recordButton, canStart);
    setButtonEnabled(m_pauseButton, status.gameplayActive && active && !status.paused);
    setButtonEnabled(m_resumeButton, status.enabled && status.gameplayActive &&
        status.paused && !status.saving);
    setButtonEnabled(m_finishButton, (active || status.paused) && !status.saving);
    setButtonEnabled(m_clipButton, status.videoSupported && !status.saving);

    if (m_pauseButton) {
        m_pauseButton->setVisible(!status.paused);
    }
    if (m_resumeButton) {
        m_resumeButton->setVisible(status.paused);
    }
}

void ReplayFloatingControl::notifyFailure(char const* fallback) {
    auto status = InstantReplay::get().status();
    auto message = status.lastError.empty() ? std::string(fallback) : status.lastError;
    Notification::create(message, NotificationIcon::Error)->show();
}

void ReplayFloatingControl::onRecord(CCObject*) {
    if (InstantReplay::get().startManualRecording()) {
        Notification::create("Iniciando grabación retroactiva...", NotificationIcon::Info)->show();
    } else {
        notifyFailure("No se pudo iniciar la grabación");
    }
    refreshState();
}

void ReplayFloatingControl::onPause(CCObject*) {
    if (InstantReplay::get().pauseManualRecording()) {
        Notification::create("Grabación pausada; búfer conservado", NotificationIcon::Info)->show();
    } else {
        notifyFailure("No se pudo pausar la grabación");
    }
    refreshState();
}

void ReplayFloatingControl::onResume(CCObject*) {
    if (InstantReplay::get().resumeManualRecording()) {
        Notification::create("Reanudando vídeo y audio...", NotificationIcon::Info)->show();
    } else {
        notifyFailure("No se pudo reanudar la grabación");
    }
    refreshState();
}

void ReplayFloatingControl::onFinish(CCObject*) {
    if (InstantReplay::get().finishManualRecording()) {
        Notification::create("Finalizando y guardando la grabación...", NotificationIcon::Info)->show();
    } else {
        notifyFailure("No se pudo finalizar la grabación");
    }
    refreshState();
}

void ReplayFloatingControl::onSaveClip(CCObject*) {
    if (InstantReplay::get().saveLast60Seconds()) {
        Notification::create("Guardando los últimos 60 segundos...", NotificationIcon::Info)->show();
    }
    refreshState();
}

} // namespace zaid::ultra
