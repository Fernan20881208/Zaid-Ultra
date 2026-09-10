#pragma once

#include <Geode/Geode.hpp>

class CCMenuItemSpriteExtra;

namespace zaid::ultra {

// A small gameplay-only replay controller. It claims touches only inside the
// ZU button; every other touch continues to Geometry Dash / external CBF.
class ReplayFloatingControl final : public cocos2d::CCLayer {
public:
    static ReplayFloatingControl* create();

    bool init() override;
    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

private:
    void onRecord(cocos2d::CCObject*);
    void onPause(cocos2d::CCObject*);
    void onResume(cocos2d::CCObject*);
    void onFinish(cocos2d::CCObject*);
    void onSaveClip(cocos2d::CCObject*);

    void setPanelVisible(bool visible);
    void setFloatingPosition(cocos2d::CCPoint position);
    void finishDrag(bool cancelled);
    void refreshState();
    void notifyFailure(char const* fallback);

    cocos2d::CCNode* m_toggleVisual = nullptr;
    cocos2d::CCLabelBMFont* m_stateLabel = nullptr;
    cocos2d::CCLabelBMFont* m_panelTitle = nullptr;
    cocos2d::CCLayerColor* m_panelBackground = nullptr;
    cocos2d::CCMenu* m_actionMenu = nullptr;
    CCMenuItemSpriteExtra* m_recordButton = nullptr;
    CCMenuItemSpriteExtra* m_pauseButton = nullptr;
    CCMenuItemSpriteExtra* m_resumeButton = nullptr;
    CCMenuItemSpriteExtra* m_finishButton = nullptr;
    CCMenuItemSpriteExtra* m_clipButton = nullptr;
    cocos2d::CCPoint m_floatingPosition{};
    cocos2d::CCPoint m_touchStart{};
    cocos2d::CCPoint m_dragOffset{};
    float m_elapsed = 0.0f;
    bool m_panelVisible = false;
    bool m_trackingTouch = false;
    bool m_dragging = false;
};

} // namespace zaid::ultra
