#include "Menu.hpp"
#include "Shared.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    ClickMenu* s_openMenu = nullptr;

    constexpr float kWidth = 400.f;
    constexpr float kHeight = 270.f;
}

ClickMenu* ClickMenu::create() {
    auto ret = new ClickMenu();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ClickMenu::isOpen() {
    return s_openMenu != nullptr;
}

void ClickMenu::toggleOpen() {
    if (s_openMenu) {
        s_openMenu->onClose(nullptr);
        return;
    }
    // Mid-level: pause first so the menu sits on top of the pause screen
    if (auto pl = PlayLayer::get()) {
        if (!pl->m_isPaused && !pl->m_hasCompletedLevel) {
            pl->pauseGame(false);
        }
    }
    if (auto menu = ClickMenu::create()) {
        menu->show();
    }
}

bool ClickMenu::init() {
    if (!Popup::init(kWidth, kHeight, "GJ_square01.png")) return false;
    s_openMenu = this;

    this->setID("click-indicator-menu"_spr);
    this->setTitle("Click Indicator", "goldFont.fnt", 0.85f, 20.f);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize({kWidth, kHeight});
    m_mainLayer->addChild(m_menu, 10);

    // Two columns of checkboxes, like the in-game options
    float leftX = 34.f;
    float rightX = kWidth / 2.f + 22.f;
    float top = kHeight - 62.f;
    float step = 33.f;

    this->addToggle("Enabled", "enabled", {leftX, top}, "Turns the whole click indicator on or off.");
    this->addToggle("Click Bars", "show-bars", {leftX, top - step}, "Green bars in the level where each click happens. Thin line = tap, wide bar = hold.");
    this->addToggle("Hit Line", "show-hit-line", {leftX, top - step * 2}, nullptr);
    this->addToggle("Click Markers", "show-markers", {leftX, top - step * 3}, nullptr);
    this->addToggle("Hide Passed", "hide-passed", {leftX, top - step * 4}, "Only shows clicks that are still ahead of you.");

    this->addToggle("Rhythm Lane", "show-lane", {rightX, top}, "The osu!mania-style lane with falling notes.");
    this->addToggle("Judgments", "show-judgments", {rightX, top - step}, "Shows PERFECT / GREAT / GOOD / MISS and how many ms early or late you clicked.");
    this->addLaneSideToggle({rightX, top - step * 2});
    this->addToggle("Practice Only", "only-practice", {rightX, top - step * 3}, "Only show the indicator in practice mode.");

    // Macro status
    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({kWidth / 2.f, 58.f});
    m_mainLayer->addChild(m_status, 10);
    this->updateStatus();

    // Bottom buttons
    auto reloadSpr = ButtonSprite::create("Reload", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    auto reloadBtn = CCMenuItemExt::createSpriteExtra(reloadSpr, [this](auto) {
        bool ok = reloadMacroForCurrentLevel();
        this->updateStatus();
        if (!PlayLayer::get()) {
            Notification::create("Open a level to load its macro", NotificationIcon::Info)->show();
        }
        else if (!ok) {
            Notification::create("No macro found for this level", NotificationIcon::Warning)->show();
        }
    });
    reloadBtn->setPosition({kWidth / 2.f - 120.f, 26.f});
    m_menu->addChild(reloadBtn);

    auto folderSpr = ButtonSprite::create("Macro Folder", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    auto folderBtn = CCMenuItemExt::createSpriteExtra(folderSpr, [](auto) {
        std::error_code ec;
        auto folder = macroFolder();
        std::filesystem::create_directories(folder, ec);
        utils::file::openFolder(folder);
    });
    folderBtn->setPosition({kWidth / 2.f, 26.f});
    m_menu->addChild(folderBtn);

    auto moreSpr = ButtonSprite::create("Settings", "goldFont.fnt", "GJ_button_04.png", 0.7f);
    auto moreBtn = CCMenuItemExt::createSpriteExtra(moreSpr, [](auto) {
        geode::openSettingsPopup(Mod::get());
    });
    moreBtn->setPosition({kWidth / 2.f + 120.f, 26.f});
    m_menu->addChild(moreBtn);

    return true;
}

void ClickMenu::addToggle(char const* text, char const* settingKey, CCPoint pos, char const* info) {
    std::string key = settingKey;
    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.75f, [key](CCMenuItemToggler* t) {
        // the callback runs before the toggler flips, so the new value is the opposite
        bool on = !t->isToggled();
        Mod::get()->setSettingValue<bool>(key, on);
        refreshOverlaySettings();
    });
    toggler->toggle(Mod::get()->getSettingValue<bool>(key));
    toggler->setPosition(pos);
    m_menu->addChild(toggler);

    auto label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({pos.x + 20.f, pos.y});
    label->limitLabelWidth(125.f, 0.5f, 0.2f);
    m_mainLayer->addChild(label, 10);

    if (info) {
        std::string title = text;
        std::string desc = info;
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.45f);
        auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [title, desc](auto) {
            FLAlertLayer::create(title.c_str(), desc, "OK")->show();
        });
        infoBtn->setPosition({pos.x - 17.f, pos.y + 12.f});
        m_menu->addChild(infoBtn);
    }
}

void ClickMenu::addLaneSideToggle(CCPoint pos) {
    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.75f, [](CCMenuItemToggler* t) {
        bool right = !t->isToggled();
        Mod::get()->setSettingValue<std::string>("lane-side", right ? "Right" : "Left");
        refreshOverlaySettings();
    });
    toggler->toggle(Mod::get()->getSettingValue<std::string>("lane-side") == "Right");
    toggler->setPosition(pos);
    m_menu->addChild(toggler);

    auto label = CCLabelBMFont::create("Lane On Right", "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({pos.x + 20.f, pos.y});
    label->limitLabelWidth(125.f, 0.5f, 0.2f);
    m_mainLayer->addChild(label, 10);
}

void ClickMenu::updateStatus() {
    if (!m_status) return;
    m_status->setString(macroStatusText().c_str());
    m_status->limitLabelWidth(kWidth - 40.f, 0.7f, 0.3f);
}

ClickMenu::~ClickMenu() {
    if (s_openMenu == this) s_openMenu = nullptr;
}

void ClickMenu::onClose(CCObject* sender) {
    if (s_openMenu == this) s_openMenu = nullptr;
    Popup::onClose(sender);
}
