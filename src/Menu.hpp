#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

// The Click Indicator menu: opened from the pause menu button or the hotkey.
class ClickMenu : public geode::Popup {
public:
    static ClickMenu* create();

    // Open the menu (pausing the level if needed), or close it if it's already open.
    static void toggleOpen();
    static bool isOpen();

    ~ClickMenu() override;

protected:
    bool init();
    void onClose(cocos2d::CCObject* sender) override;

private:
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;

    void addToggle(char const* text, char const* settingKey, cocos2d::CCPoint pos, char const* info);
    void addLaneSideToggle(cocos2d::CCPoint pos);
    void updateStatus();
};
