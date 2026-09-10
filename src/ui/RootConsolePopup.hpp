#pragma once

#include <Geode/ui/Popup.hpp>

namespace geode {
class SimpleTextArea;
}

namespace zaid::ultra {

class RootConsolePopup final : public geode::Popup {
public:
    static RootConsolePopup* create();

protected:
    bool init() override;
    void update(float dt) override;

private:
    void refreshText();
    void onRefresh(cocos2d::CCObject*);
    void onTrim(cocos2d::CCObject*);
    void onRestore(cocos2d::CCObject*);
    void onSaveReplay(cocos2d::CCObject*);

    geode::SimpleTextArea* m_text = nullptr;
    float m_elapsed = 0.0f;
};

} // namespace zaid::ultra
