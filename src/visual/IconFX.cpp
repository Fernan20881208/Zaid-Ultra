#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {

CCSprite* makeBloomShell(CCSprite* source, CCNode* parent, int zOffset) {
    if (!source || !parent || !source->getTexture()) return nullptr;

    auto shell = CCSprite::createWithTexture(source->getTexture(), source->getTextureRect());
    if (!shell) return nullptr;

    shell->setAnchorPoint(source->getAnchorPoint());
    shell->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    shell->setOpacity(0);
    parent->addChild(shell, source->getZOrder() + zOffset);
    return shell;
}

void syncShell(CCSprite* shell, CCSprite* source, float expansion, GLubyte opacity) {
    if (!shell || !source) return;

    shell->setTexture(source->getTexture());
    shell->setTextureRect(source->getTextureRect());
    shell->setPosition(source->getPosition());
    shell->setRotation(source->getRotation());
    shell->setFlipX(source->isFlipX());
    shell->setFlipY(source->isFlipY());
    shell->setColor(source->getColor());
    shell->setScaleX(source->getScaleX() * expansion);
    shell->setScaleY(source->getScaleY() * expansion);
    shell->setOpacity(opacity);
    shell->setVisible(true);
}

} // namespace

class $modify(ZaidUltraPlayerObject, PlayerObject) {
    struct Fields {
        CCSprite* m_bloomInner = nullptr;
        CCSprite* m_bloomOuter = nullptr;
        CCSprite* m_source = nullptr;
        float m_phase = 0.f;
    };

    void update(float dt) {
        PlayerObject::update(dt);

        const bool enabled = Mod::get()->getSettingValue<bool>("core-enabled") &&
            Mod::get()->getSettingValue<bool>("rtx-bloom");

        auto fields = m_fields.self();
        if (!enabled || !m_iconGlow || !m_iconGlow->getParent()) {
            if (fields->m_bloomInner) fields->m_bloomInner->setVisible(false);
            if (fields->m_bloomOuter) fields->m_bloomOuter->setVisible(false);
            return;
        }

        auto source = m_iconGlow;
        if (fields->m_source != source || !fields->m_bloomInner || !fields->m_bloomOuter) {
            if (fields->m_bloomInner) fields->m_bloomInner->removeFromParentAndCleanup(true);
            if (fields->m_bloomOuter) fields->m_bloomOuter->removeFromParentAndCleanup(true);

            fields->m_source = source;
            fields->m_bloomInner = makeBloomShell(source, source->getParent(), -1);
            fields->m_bloomOuter = makeBloomShell(source, source->getParent(), -2);
        }

        source->setVisible(true);
        fields->m_phase += std::clamp(dt, 0.f, .05f) * 7.f;

        const float intensity = std::clamp(
            Mod::get()->getSavedValue<float>("rtx-bloom-intensity", 1.f),
            .5f,
            2.f
        );
        const float pulse = 1.f + std::sin(fields->m_phase) * .035f * intensity;

        const auto innerOpacity = static_cast<GLubyte>(std::clamp(92.f * intensity, 20.f, 190.f));
        const auto outerOpacity = static_cast<GLubyte>(std::clamp(48.f * intensity, 10.f, 130.f));

        syncShell(fields->m_bloomInner, source, (1.16f + .05f * intensity) * pulse, innerOpacity);
        syncShell(fields->m_bloomOuter, source, (1.38f + .10f * intensity) * pulse, outerOpacity);
    }
};
