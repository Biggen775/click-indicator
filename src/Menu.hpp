#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <vector>

// The Click Indicator menu: opened from the pause menu button or the hotkey.
class ClickMenu : public geode::Popup {
public:
    static ClickMenu* create();
    static void toggleOpen();
    static bool isOpen();

    ~ClickMenu() override;

protected:
    bool init();
    void onClose(cocos2d::CCObject* sender) override;

private:
    int m_page = 0;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCNode* m_pageNode = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_strengthLabel = nullptr;
    cocos2d::CCLabelBMFont* m_releaseLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemToggler* m_recordToggle = nullptr;

    void buildPage();
    void addToggle(cocos2d::CCNode* parent, cocos2d::CCMenu* menu, char const* text, char const* settingKey, cocos2d::CCPoint pos, char const* info);
    void addChoiceToggle(cocos2d::CCNode* parent, cocos2d::CCMenu* menu, char const* text, char const* settingKey, char const* onValue, char const* offValue, cocos2d::CCPoint pos, char const* info);
    void addNumberRow(cocos2d::CCNode* parent, cocos2d::CCMenu* menu, char const* text, char const* settingKey, int64_t lo, int64_t hi, int64_t step, cocos2d::CCPoint pos, cocos2d::CCLabelBMFont** label, char const* suffix);
    void addLabel(cocos2d::CCNode* parent, char const* text, cocos2d::CCPoint pos, float scale);
    void addCueSoundRow(cocos2d::CCNode* parent, cocos2d::CCMenu* menu, cocos2d::CCPoint pos);
    void updateNumberLabels();
    void updateStatus();
    void openMacroPicker();
};
