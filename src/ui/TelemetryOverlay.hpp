#pragma once

#include <Geode/Geode.hpp>

namespace zaid::ultra {

class TelemetryOverlay final : public cocos2d::CCNode {
public:
    static TelemetryOverlay* create();
    bool init() override;
    void update(float dt) override;

private:
    cocos2d::CCLabelBMFont* m_label = nullptr;
    float m_elapsed = 0.0f;
};

} // namespace zaid::ultra
