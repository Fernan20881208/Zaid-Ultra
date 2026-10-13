#include "TelemetryOverlay.hpp"

#include "../modules/TelemetryManager.hpp"

using namespace geode::prelude;

namespace zaid::ultra {

TelemetryOverlay* TelemetryOverlay::create() {
    auto* result = new TelemetryOverlay();
    if (result && result->init()) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool TelemetryOverlay::init() {
    if (!CCNode::init()) {
        return false;
    }
    auto size = CCDirector::sharedDirector()->getWinSize();
    m_label = CCLabelBMFont::create("Zaid-Ultra: midiendo...", "chatFont.fnt");
    m_label->setAnchorPoint({0.0f, 1.0f});
    m_label->setAlignment(kCCTextAlignmentLeft);
    m_label->setScale(0.34f);
    m_label->setOpacity(205);
    m_label->setPosition({5.0f, size.height - 5.0f});
    this->addChild(m_label);
    this->scheduleUpdate();
    return true;
}

void TelemetryOverlay::update(float dt) {
    m_elapsed += dt;
    if (m_elapsed < 0.25f || !m_label) {
        return;
    }
    m_elapsed = 0.0f;
    m_label->setString(TelemetryManager::get().compactText().c_str());
}

} // namespace zaid::ultra
