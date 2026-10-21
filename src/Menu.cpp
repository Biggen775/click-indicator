#include "Menu.hpp"
#include "Shared.hpp"
#include "Overlay.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    ClickMenu* s_openMenu = nullptr;

    constexpr float kWidth = 400.f;
    constexpr float kHeight = 296.f;
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
    float top = kHeight - 56.f;
    float step = 30.f;

    this->addToggle("Enabled", "enabled", {leftX, top}, "Turns the whole click indicator on or off.");
    this->addToggle("Click Bars", "show-bars", {leftX, top - step}, "Green bars in the level where each click happens. Thin line = tap, wide bar = hold.");
    this->addToggle("Hit Line", "show-hit-line", {leftX, top - step * 2}, nullptr);
    this->addToggle("Click Markers", "show-markers", {leftX, top - step * 3}, nullptr);
    this->addToggle("Hide Passed", "hide-passed", {leftX, top - step * 4}, "Only shows clicks that are still ahead of you.");

    this->addToggle("Rhythm Lane", "show-lane", {rightX, top}, "The osu!mania-style lane with falling notes.");
    this->addToggle("Judgments", "show-judgments", {rightX, top - step}, "Shows PERFECT / GREAT / GOOD / MISS and how many ms early or late you clicked.");
    this->addLaneSideToggle({rightX, top - step * 2});
    this->addToggle("Practice Only", "only-practice", {rightX, top - step * 3}, "Only show the indicator in practice mode.");
    this->addToggle("Aim Assist", "assist-enabled", {rightX, top - step * 4}, "If you click EARLY, your click is held back and sent at the exact moment the macro clicked. Late clicks go through normally. At strength 5 and up, letting go early does NOT end the hold: it keeps holding until the macro lets go. With Safe Mode on (in Settings), progress isn't saved while it's on.");

    this->addStrengthRow(top - step * 5 + 2.f);

    // Macro status
    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({kWidth / 2.f, 60.f});
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

void ClickMenu::addStrengthRow(float y) {
    auto title = CCLabelBMFont::create("Assist Strength", "bigFont.fnt");
    title->setScale(0.45f);
    title->setPosition({kWidth / 2.f - 70.f, y});
    m_mainLayer->addChild(title, 10);

    m_strengthLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_strengthLabel->setPosition({kWidth / 2.f + 90.f, y});
    m_mainLayer->addChild(m_strengthLabel, 10);

    auto makeArrow = [this, y](bool right) {
        auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        spr->setScale(0.45f);
        if (!right) spr->setFlipX(true);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, right](auto) {
            auto mod = Mod::get();
            int64_t v = mod->getSettingValue<int64_t>("assist-strength") + (right ? 1 : -1);
            v = std::clamp<int64_t>(v, 1, 10);
            mod->setSettingValue<int64_t>("assist-strength", v);
            refreshOverlaySettings();
            this->updateStrength();
        });
        btn->setPosition({kWidth / 2.f + 90.f + (right ? 52.f : -52.f), y});
        m_menu->addChild(btn);
    };
    makeArrow(false);
    makeArrow(true);
    this->updateStrength();
}

void ClickMenu::updateStrength() {
    if (!m_strengthLabel) return;
    int64_t v = Mod::get()->getSettingValue<int64_t>("assist-strength");
    double ms = ClickOverlay::assistWindowFor(static_cast<int>(v)) * 1000.0;
    m_strengthLabel->setString(fmt::format("{} ({:.0f}ms)", v, ms).c_str());
    m_strengthLabel->limitLabelWidth(80.f, 0.45f, 0.2f);
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
