#include "ZaidUltraPopup.hpp"

#include "../core/RunPredictor.hpp"
#include "../core/SmartDetail.hpp"
#include "../core/Telemetry.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace zaidultra {

namespace {

constexpr char const* kTabNames[] = {
    "Home",
    "Performance",
    "Smart Detail",
    "Gameplay",
    "Icon FX",
    "Android",
};

char const* tierName(DetailTier tier) {
    switch (tier) {
        case DetailTier::LDM: return "LDM";
        case DetailTier::ULDM: return "ULDM";
        default: return "Normal";
    }
}

} // namespace

ZaidUltraPopup* ZaidUltraPopup::create(UltraTab initialTab) {
    auto popup = new ZaidUltraPopup();
    if (popup->init(initialTab)) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool ZaidUltraPopup::init(UltraTab initialTab) {
    if (!Popup::init(440.f, 270.f)) {
        return false;
    }

    this->setTitle("Zaid-Ultra", "goldFont.fnt", 0.72f, 18.f);
    m_tab = initialTab;

    m_content = CCNode::create();
    m_content->setContentSize({310.f, 205.f});
    m_content->setAnchorPoint({0.5f, 0.5f});
    m_content->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(m_content, Anchor::Center, {52.f, -5.f});

    buildTabs();
    renderTab();
    return true;
}

void ZaidUltraPopup::buildTabs() {
    auto menu = CCMenu::create();
    menu->setContentSize({108.f, 205.f});
    menu->setAnchorPoint({0.5f, 0.5f});
    menu->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(menu, Anchor::Center, {-158.f, -5.f});

    for (int i = 0; i < 6; ++i) {
        auto sprite = ButtonSprite::create(kTabNames[i]);
        sprite->setScale(0.47f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(ZaidUltraPopup::onTab)
        );
        button->setTag(i);
        button->setPosition({54.f, 181.f - static_cast<float>(i) * 32.f});
        menu->addChild(button);
    }
}

void ZaidUltraPopup::clearContent() {
    m_content->removeAllChildren();

    m_contentMenu = CCMenu::create();
    m_contentMenu->setContentSize(m_content->getContentSize());
    m_contentMenu->setAnchorPoint({0.f, 0.f});
    m_contentMenu->ignoreAnchorPointForPosition(false);
    m_contentMenu->setPosition({0.f, 0.f});
    m_content->addChild(m_contentMenu, 5);
}

void ZaidUltraPopup::addHeader(char const* title, char const* subtitle) {
    auto titleLabel = CCLabelBMFont::create(title, "bigFont.fnt");
    titleLabel->setScale(0.53f);
    titleLabel->setPosition({155.f, 183.f});
    m_content->addChild(titleLabel);

    auto subtitleLabel = CCLabelBMFont::create(subtitle, "chatFont.fnt");
    subtitleLabel->setScale(0.50f);
    subtitleLabel->setOpacity(190);
    subtitleLabel->setPosition({155.f, 160.f});
    m_content->addChild(subtitleLabel);
}

void ZaidUltraPopup::addToggleRow(char const* label, char const* settingKey, int tag, float y) {
    auto text = CCLabelBMFont::create(label, "bigFont.fnt");
    text->setScale(0.40f);
    text->setAnchorPoint({0.f, 0.5f});
    text->setPosition({15.f, y});
    m_content->addChild(text);

    auto toggle = CCMenuItemToggler::createWithStandardSprites(
        this,
        menu_selector(ZaidUltraPopup::onFeatureToggle),
        0.65f
    );
    toggle->setTag(tag);
    toggle->setUserObject(CCString::create(settingKey));
    toggle->toggle(Mod::get()->getSettingValue<bool>(settingKey));
    toggle->setPosition({286.f, y});
    m_contentMenu->addChild(toggle);
}

void ZaidUltraPopup::addValueRow(char const* label, std::string const& value, float y) {
    auto left = CCLabelBMFont::create(label, "chatFont.fnt");
    left->setScale(0.56f);
    left->setAnchorPoint({0.f, 0.5f});
    left->setPosition({16.f, y});
    left->setOpacity(205);
    m_content->addChild(left);

    auto right = CCLabelBMFont::create(value.c_str(), "bigFont.fnt");
    right->setScale(0.37f);
    right->setAnchorPoint({1.f, 0.5f});
    right->setPosition({294.f, y});
    m_content->addChild(right);
}

void ZaidUltraPopup::renderTab() {
    clearContent();

    const auto frame = Telemetry::get().snapshot();
    const auto run = RunPredictor::get().prediction();
    const auto detail = SmartDetail::get().decision(Telemetry::get().currentPercent(), 120.f);

    switch (m_tab) {
        case UltraTab::Dashboard:
            addHeader("Ultra Dashboard", "One suite. Shared telemetry. Zero duplicated profilers.");
            addValueRow("Frame pacing", fmt::format("{:.0f}/100", frame.framePacingScore), 128.f);
            addValueRow("Benchmark", fmt::format("{:.0f}/100", Telemetry::get().performanceScore(120.f)), 101.f);
            addValueRow("Run momentum", fmt::format("{:.0f}%", run.pbMomentum), 74.f);
            addValueRow("Smart detail", tierName(SmartDetail::get().activeTier()), 47.f);
            break;

        case UltraTab::Performance:
            addHeader("Performance Lab", "Frame Doctor + Guardian + automatic benchmark");
            addToggleRow("Frame Doctor", "frame-doctor", 100, 128.f);
            addToggleRow("Android Guardian", "android-guardian", 101, 99.f);
            addToggleRow("Auto Benchmark", "auto-benchmark", 102, 70.f);
            addValueRow("Avg / 1% low", fmt::format("{:.2f} ms / {:.0f} FPS", frame.averageFrameMs, frame.onePercentLowFps), 39.f);
            break;

        case UltraTab::SmartDetail:
            addHeader("Smart LDM / ULDM", "Learns heavy sections and predicts them before the drop");
            addToggleRow("Adaptive detail", "smart-detail", 103, 126.f);
            addValueRow("Active tier", tierName(SmartDetail::get().activeTier()), 94.f);
            addValueRow("Predicted tier", tierName(detail.tier), 69.f);
            addValueRow("Learned / budget", fmt::format("{:.2f} / {:.2f} ms", detail.learnedFrameMs, detail.budgetMs), 44.f);
            break;

        case UltraTab::Gameplay:
            addHeader("Player Intelligence", "Run Predictor learns only from your own attempts");
            addToggleRow("Run Predictor", "run-predictor", 104, 126.f);
            addValueRow("Current / PB", fmt::format("{:.1f}% / {:.1f}%", run.currentPercent, run.sessionBest), 94.f);
            addValueRow("PB momentum", fmt::format("{:.0f}%", run.pbMomentum), 68.f);
            addValueRow("Completion confidence", fmt::format("{:.0f}%", run.completionConfidence), 42.f);
            break;

        case UltraTab::IconFX: {
            addHeader("Icon FX", "Reactive RTX-style bloom layered over Geometry Dash glow");
            addToggleRow("Reactive RTX Bloom", "rtx-bloom", 105, 126.f);
            const float intensity = Mod::get()->getSavedValue<float>("rtx-bloom-intensity", 1.f);
            addValueRow("Bloom intensity", fmt::format("{:.1f}x", intensity), 93.f);

            auto minus = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("-"), this, menu_selector(ZaidUltraPopup::onBloomIntensity)
            );
            minus->setTag(-1);
            minus->setScale(0.62f);
            minus->setPosition({220.f, 61.f});
            m_contentMenu->addChild(minus);

            auto plus = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("+"), this, menu_selector(ZaidUltraPopup::onBloomIntensity)
            );
            plus->setTag(1);
            plus->setScale(0.62f);
            plus->setPosition({270.f, 61.f});
            m_contentMenu->addChild(plus);

            addValueRow("Render path", "Glow + 2 additive bloom shells", 35.f);
            break;
        }

        case UltraTab::Android:
            addHeader("Android Guardian", "Mobile-first performance policy for high-refresh gameplay");
            addToggleRow("Guardian", "android-guardian", 106, 126.f);
            addValueRow("Target frame budget", "120 FPS / 8.33 ms", 94.f);
            addValueRow("Smart Detail link", Mod::get()->getSettingValue<bool>("smart-detail") ? "Connected" : "Disabled", 68.f);
            addValueRow("Thermal sensors", "Platform adapter next", 42.f);
            break;
    }
}

void ZaidUltraPopup::onTab(CCObject* sender) {
    m_tab = static_cast<UltraTab>(sender->getTag());
    renderTab();
}

void ZaidUltraPopup::onFeatureToggle(CCObject* sender) {
    auto node = static_cast<CCNode*>(sender);
    auto keyObj = static_cast<CCString*>(node->getUserObject());
    if (!keyObj) {
        return;
    }

    const char* key = keyObj->getCString();
    const bool value = !Mod::get()->getSettingValue<bool>(key);
    Mod::get()->setSettingValue<bool>(key, value);

    auto toggle = static_cast<CCMenuItemToggler*>(sender);
    toggle->toggle(value);
}

void ZaidUltraPopup::onBloomIntensity(CCObject* sender) {
    const float current = Mod::get()->getSavedValue<float>("rtx-bloom-intensity", 1.f);
    const float next = std::clamp(current + static_cast<float>(sender->getTag()) * 0.1f, 0.5f, 2.0f);
    Mod::get()->setSavedValue<float>("rtx-bloom-intensity", next);
    renderTab();
}

} // namespace zaidultra
