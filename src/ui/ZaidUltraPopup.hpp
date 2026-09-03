#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace zaidultra {

enum class UltraTab : int {
    Dashboard = 0,
    Performance = 1,
    SmartDetail = 2,
    Gameplay = 3,
    IconFX = 4,
    Android = 5,
};

class ZaidUltraPopup final : public geode::Popup {
public:
    static ZaidUltraPopup* create(UltraTab initialTab = UltraTab::Dashboard);

protected:
    bool init(UltraTab initialTab);

    void buildTabs();
    void renderTab();
    void clearContent();
    void addHeader(char const* title, char const* subtitle);
    void addToggleRow(char const* label, char const* settingKey, int tag, float y);
    void addValueRow(char const* label, std::string const& value, float y);

    void onTab(cocos2d::CCObject* sender);
    void onFeatureToggle(cocos2d::CCObject* sender);
    void onBloomIntensity(cocos2d::CCObject* sender);

    UltraTab m_tab = UltraTab::Dashboard;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCMenu* m_contentMenu = nullptr;
};

} // namespace zaidultra
