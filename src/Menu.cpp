#include "Menu.hpp"
#include "Shared.hpp"
#include "Overlay.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    ClickMenu* s_openMenu = nullptr;

    constexpr float kWidth = 420.f;
    constexpr float kHeight = 318.f;
    constexpr int kPages = 4;
    constexpr char const* kPageNames[kPages] = {"Display", "Aim Assist", "Practice", "Tools"};

    void settingChanged() {
        refreshOverlaySettings();
        savePerLevelSettings();
    }

    // Stats popup: text + a bar chart of accuracy per attempt
    class StatsPopup : public Popup {
    public:
        static StatsPopup* create() {
            auto ret = new StatsPopup();
            if (ret->init(360.f, 270.f, "GJ_square01.png")) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

    private:
        bool init(float w, float h, char const* bg) {
            if (!Popup::init(w, h, bg)) return false;
            this->setTitle("Attempt Stats", "goldFont.fnt", 0.8f, 18.f);

            auto text = CCLabelBMFont::create(currentStatsText().c_str(), "chatFont.fnt");
            text->setAlignment(kCCTextAlignmentLeft);
            text->setAnchorPoint({0.f, 1.f});
            text->setPosition({18.f, h - 40.f});
            text->setScale(0.55f);
            text->limitLabelWidth(w - 36.f, 0.55f, 0.3f);
            m_mainLayer->addChild(text, 10);

            auto hist = attemptHistory();
            auto label = CCLabelBMFont::create(fmt::format("Accuracy over the last {} attempts", hist.size()).c_str(), "bigFont.fnt");
            label->setScale(0.32f);
            label->setPosition({w / 2.f, 96.f});
            m_mainLayer->addChild(label, 10);

            auto draw = CCDrawNode::create();
            m_mainLayer->addChild(draw, 10);
            float x0 = 30.f, x1 = w - 30.f, y0 = 22.f, y1 = 84.f;
            draw->drawRect(ccp(x0, y0), ccp(x1, y1), ccc4f(0.f, 0.f, 0.f, 0.35f), 0.f, ccc4f(0, 0, 0, 0));
            for (int pct : {50, 100}) {
                float y = y0 + (y1 - y0) * pct / 100.f;
                draw->drawSegment(ccp(x0, y), ccp(x1, y), 0.4f, ccc4f(1.f, 1.f, 1.f, 0.15f));
            }
            if (!hist.empty()) {
                float bw = (x1 - x0) / static_cast<float>(hist.size());
                for (size_t i = 0; i < hist.size(); i++) {
                    float v = static_cast<float>(std::clamp(hist[i], 0.0, 100.0)) / 100.f;
                    float bx = x0 + bw * i + 1.f;
                    auto c = v >= 0.9f ? ccc4f(0.35f, 1.f, 0.5f, 0.9f) : v >= 0.7f ? ccc4f(1.f, 0.9f, 0.3f, 0.9f) : ccc4f(1.f, 0.4f, 0.4f, 0.9f);
                    draw->drawRect(ccp(bx, y0), ccp(bx + std::max(bw - 2.f, 1.f), y0 + (y1 - y0) * v), c, 0.f, ccc4f(0, 0, 0, 0));
                }
            }
            else {
                auto none = CCLabelBMFont::create("no finished attempts yet", "chatFont.fnt");
                none->setScale(0.5f);
                none->setPosition({w / 2.f, (y0 + y1) / 2.f});
                m_mainLayer->addChild(none, 11);
            }
            return true;
        }
    };

    // Small popup listing the macro files in the folder
    class MacroPicker : public Popup {
    public:
        static MacroPicker* create(std::function<void()> onPick) {
            auto ret = new MacroPicker();
            ret->m_onPick = std::move(onPick);
            if (ret->init(300.f, 240.f, "GJ_square01.png")) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

    private:
        std::function<void()> m_onPick;

        bool init(float w, float h, char const* bg) {
            if (!Popup::init(w, h, bg)) return false;
            this->setTitle("Pick a Macro", "goldFont.fnt", 0.8f, 18.f);

            auto files = listMacroFiles();
            auto current = macroOverrideForCurrentLevel();

            auto scroll = ScrollLayer::create({w - 40.f, h - 95.f});
            scroll->setPosition({20.f, 48.f});
            scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
            m_mainLayer->addChild(scroll, 10);

            if (files.empty()) {
                auto lbl = CCLabelBMFont::create("No macro files in the folder yet", "chatFont.fnt");
                lbl->setScale(0.6f);
                lbl->setPosition({w / 2.f, h / 2.f});
                m_mainLayer->addChild(lbl, 10);
            }

            for (auto const& f : files) {
                auto row = CCMenu::create();
                row->setContentSize({w - 40.f, 24.f});
                row->setPosition({0.f, 0.f});
                row->ignoreAnchorPointForPosition(false);

                bool isCurrent = f == current;
                auto spr = ButtonSprite::create(f.c_str(), static_cast<int>(w - 60.f), true, "chatFont.fnt", isCurrent ? "GJ_button_02.png" : "GJ_button_04.png", 24.f, 0.6f);
                auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, f](auto) {
                    useMacroForCurrentLevel(f);
                    if (m_onPick) m_onPick();
                    this->onClose(nullptr);
                });
                btn->setPosition({(w - 40.f) / 2.f, 12.f});
                row->addChild(btn);
                scroll->m_contentLayer->addChild(row);
            }
            scroll->m_contentLayer->updateLayout();
            scroll->scrollToTop();

            auto autoSpr = ButtonSprite::create("Automatic", "goldFont.fnt", "GJ_button_01.png", 0.6f);
            auto autoBtn = CCMenuItemExt::createSpriteExtra(autoSpr, [this](auto) {
                useMacroForCurrentLevel("");
                if (m_onPick) m_onPick();
                this->onClose(nullptr);
            });
            autoBtn->setPosition({w / 2.f, 26.f});
            m_buttonMenu->addChild(autoBtn);
            return true;
        }
    };
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

bool ClickMenu::isOpen() { return s_openMenu != nullptr; }

void ClickMenu::toggleOpen() {
    if (s_openMenu) {
        s_openMenu->onClose(nullptr);
        return;
    }
    if (auto pl = PlayLayer::get()) {
        if (!pl->m_isPaused && !pl->m_hasCompletedLevel) pl->pauseGame(false);
    }
    if (auto menu = ClickMenu::create()) menu->show();
}

ClickMenu::~ClickMenu() {
    if (s_openMenu == this) s_openMenu = nullptr;
}

bool ClickMenu::init() {
    if (!Popup::init(kWidth, kHeight, "GJ_square01.png")) return false;
    s_openMenu = this;
    m_page = Mod::get()->getSavedValue<int64_t>("menu-page", 0);
    if (m_page < 0 || m_page >= kPages) m_page = 0;

    this->setID("click-indicator-menu"_spr);
    this->setTitle("Click Indicator", "goldFont.fnt", 0.85f, 20.f);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize({kWidth, kHeight});
    m_mainLayer->addChild(m_menu, 10);

    // page arrows + name
    m_pageLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_pageLabel->setScale(0.45f);
    m_pageLabel->setPosition({kWidth / 2.f, kHeight - 44.f});
    m_mainLayer->addChild(m_pageLabel, 10);

    auto makeArrow = [this](bool right) {
        auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        spr->setScale(0.6f);
        if (!right) spr->setFlipX(true);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, right](auto) {
            m_page = (m_page + (right ? 1 : kPages - 1)) % kPages;
            Mod::get()->setSavedValue<int64_t>("menu-page", m_page);
            this->buildPage();
        });
        btn->setPosition({kWidth / 2.f + (right ? 90.f : -90.f), kHeight - 44.f});
        m_menu->addChild(btn);
    };
    makeArrow(false);
    makeArrow(true);

    // status line + bottom buttons (shared by all pages)
    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({kWidth / 2.f, 54.f});
    m_mainLayer->addChild(m_status, 10);

    auto reloadSpr = ButtonSprite::create("Reload", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto reloadBtn = CCMenuItemExt::createSpriteExtra(reloadSpr, [this](auto) {
        bool ok = reloadMacroForCurrentLevel();
        this->updateStatus();
        if (!PlayLayer::get()) Notification::create("Open a level to load its macro", NotificationIcon::Info)->show();
        else if (!ok) Notification::create("No macro found for this level", NotificationIcon::Warning)->show();
    });
    reloadBtn->setPosition({kWidth / 2.f - 150.f, 24.f});
    m_menu->addChild(reloadBtn);

    auto pickSpr = ButtonSprite::create("Pick Macro", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto pickBtn = CCMenuItemExt::createSpriteExtra(pickSpr, [this](auto) { this->openMacroPicker(); });
    pickBtn->setPosition({kWidth / 2.f - 50.f, 24.f});
    m_menu->addChild(pickBtn);

    auto folderSpr = ButtonSprite::create("Folder", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto folderBtn = CCMenuItemExt::createSpriteExtra(folderSpr, [](auto) {
        std::error_code ec;
        auto folder = macroFolder();
        std::filesystem::create_directories(folder, ec);
        utils::file::openFolder(folder);
    });
    folderBtn->setPosition({kWidth / 2.f + 50.f, 24.f});
    m_menu->addChild(folderBtn);

    auto moreSpr = ButtonSprite::create("Settings", "goldFont.fnt", "GJ_button_04.png", 0.65f);
    auto moreBtn = CCMenuItemExt::createSpriteExtra(moreSpr, [](auto) { geode::openSettingsPopup(Mod::get()); });
    moreBtn->setPosition({kWidth / 2.f + 150.f, 24.f});
    m_menu->addChild(moreBtn);

    this->buildPage();
    this->updateStatus();
    return true;
}

void ClickMenu::buildPage() {
    if (m_pageNode) m_pageNode->removeFromParent();
    m_pageNode = CCNode::create();
    m_pageNode->setPosition({0.f, 0.f});
    m_pageNode->setContentSize({kWidth, kHeight});
    m_mainLayer->addChild(m_pageNode, 10);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    m_pageNode->addChild(menu, 10);

    m_pageLabel->setString(kPageNames[m_page]);
    m_strengthLabel = nullptr;
    m_releaseLabel = nullptr;
    m_recordToggle = nullptr;

    float leftX = 34.f;
    float rightX = kWidth / 2.f + 22.f;
    float top = kHeight - 74.f;
    float step = 28.f;

    if (m_page == 0) {
        this->addToggle(m_pageNode, menu, "Enabled", "enabled", {leftX, top}, "Turns the whole click indicator on or off. Also toggled by the Overlay hotkey (F7 by default).");
        this->addToggle(m_pageNode, menu, "Click Bars", "show-bars", {leftX, top - step}, "Green bars in the level where each click happens. Thin line = tap, wide bar = hold.");
        this->addToggle(m_pageNode, menu, "Hit Line", "show-hit-line", {leftX, top - step * 2}, nullptr);
        this->addToggle(m_pageNode, menu, "Click Markers", "show-markers", {leftX, top - step * 3}, nullptr);
        this->addToggle(m_pageNode, menu, "Hide Passed", "hide-passed", {leftX, top - step * 4}, "Only shows clicks that are still ahead of you.");
        this->addToggle(m_pageNode, menu, "Trouble Spots", "trouble-spots", {leftX, top - step * 5}, "Clicks you keep getting wrong (late, early or missed) across attempts turn red so you know what to practice. Hitting them well again clears the red.");

        this->addToggle(m_pageNode, menu, "Rhythm Lane", "show-lane", {rightX, top}, "The osu!mania-style lane with falling notes.");
        this->addChoiceToggle(m_pageNode, menu, "Horizontal Lane", "lane-style", "Horizontal", "Vertical", {rightX, top - step}, "Guitar Hero style: notes come in from the right along the bottom of the screen instead of a vertical tower.");
        this->addChoiceToggle(m_pageNode, menu, "Lane On Right", "lane-side", "Right", "Left", {rightX, top - step * 2}, nullptr);
        this->addToggle(m_pageNode, menu, "Judgments", "show-judgments", {rightX, top - step * 3}, "Shows PERFECT / GREAT / GOOD / MISS and how many ms early or late you clicked.");
        this->addToggle(m_pageNode, menu, "Click Sounds", "tick-sound", {rightX, top - step * 4}, "Plays a tick at every macro click so you can learn the rhythm by ear. Settings has a volume slider and a separate tick for releases.");
        this->addToggle(m_pageNode, menu, "Practice Only", "only-practice", {rightX, top - step * 5}, "Only show the indicator in practice mode.");
    }
    else if (m_page == 1) {
        this->addToggle(m_pageNode, menu, "Aim Assist", "assist-enabled", {leftX, top}, "If you click EARLY, your click is held back and sent at the exact moment the macro clicked. Late clicks go through normally. At release strength 5 and up, letting go early does NOT end the hold. Also toggled by the Assist hotkey (F9 by default).");
        this->addToggle(m_pageNode, menu, "Assist Flash", "assist-flash", {leftX, top - step}, "Flashes the lane receptor yellow and shows ASSIST whenever a click was corrected.");
        this->addToggle(m_pageNode, menu, "Safe Mode", "assist-safe-mode", {leftX, top - step * 2}, "While the assist is on, the level runs like a start-pos test so no percentage or completion is saved.");
        this->addToggle(m_pageNode, menu, "Per-Level Settings", "per-level", {leftX, top - step * 3}, "Remembers the settings on these pages separately for every level.");

        this->addNumberRow(m_pageNode, menu, "Press Strength", "assist-strength", 1, 10, 1, {rightX + 60.f, top}, &m_strengthLabel, "");
        this->addNumberRow(m_pageNode, menu, "Release Strength", "assist-release-strength", 0, 10, 1, {rightX + 60.f, top - step * 1.3f}, &m_releaseLabel, "");
        this->addNumberRow(m_pageNode, menu, "Assist Off At %", "assist-off-percent", 0, 100, 5, {rightX + 60.f, top - step * 2.6f}, nullptr, "%");
        this->addNumberRow(m_pageNode, menu, "Hide Overlay At %", "hide-after-percent", 0, 100, 5, {rightX + 60.f, top - step * 3.9f}, nullptr, "%");
        this->addLabel(m_pageNode, "0 = never. Release 0 = same as press.", {rightX + 60.f, top - step * 5.f}, 0.4f);
        auto resetSpr = ButtonSprite::create("Reset Level Prefs", "goldFont.fnt", "GJ_button_06.png", 0.5f);
        auto resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr, [](auto) {
            clearPerLevelSettings();
            Notification::create("Per-level settings cleared for this level", NotificationIcon::Info)->show();
        });
        resetBtn->setPosition({leftX + 60.f, top - step * 4.4f});
        menu->addChild(resetBtn);
    }
    else if (m_page == 2) {
        this->addToggle(m_pageNode, menu, "Auto-Play", "autoplay", {leftX, top}, "The mod plays the macro for you so you can watch the clicks and the lane in sync. Your own clicks are ignored while it's on. Progress is never saved in auto-play.");
        this->addNumberRow(m_pageNode, menu, "Auto-Play Speed %", "autoplay-speed-pct", 25, 100, 5, {leftX + 70.f, top - step * 1.5f}, nullptr, "%");
        this->addToggle(m_pageNode, menu, "Count-In Beats", "count-in", {leftX, top - step * 3}, "Three soft ticks half a second apart before the first click after a restart or checkpoint.");
        this->addToggle(m_pageNode, menu, "Progressive Assist", "progressive-assist", {leftX, top - step * 4}, "Every time you finish a run or section with no misses and at least the target accuracy, the assist strength drops by one.");
        this->addNumberRow(m_pageNode, menu, "Clean Run Target %", "progressive-target", 50, 100, 5, {leftX + 70.f, top - step * 5.5f}, nullptr, "%");

        this->addToggle(m_pageNode, menu, "Section Practice", "section-practice", {rightX, top}, "Drill one part of the level. In practice mode a checkpoint is placed when you reach the start percent, and the level restarts when you reach the end percent. Stats count the section.");
        this->addNumberRow(m_pageNode, menu, "Section Start %", "section-start", 0, 99, 1, {rightX + 60.f, top - step * 1.5f}, nullptr, "%");
        this->addNumberRow(m_pageNode, menu, "Section End %", "section-end", 0, 100, 1, {rightX + 60.f, top - step * 3.f}, nullptr, "%");
        this->addLabel(m_pageNode, "End 0 or 100 = play to the end", {rightX + 60.f, top - step * 4.1f}, 0.4f);
        this->addToggle(m_pageNode, menu, "Timeline", "timeline", {rightX, top - step * 5.f}, "A thin bar across the top with every click as a tick mark (red = trouble spot), your position, and the section markers.");
    }
    else {
        // Tools: recording + stats
        this->addLabel(m_pageNode, "Record Your Own Macro", {kWidth / 2.f, top + 4.f}, 0.5f);
        this->addLabel(m_pageNode, "Turn on, play in practice mode to 100% and it saves\nautomatically. Dying keeps the clicks from before the checkpoint.", {kWidth / 2.f, top - 20.f}, 0.42f);

        auto rec = CCMenuItemExt::createTogglerWithStandardSprites(0.75f, [this](CCMenuItemToggler* t) {
            bool on = !t->isToggled();
            setRecording(on);
            this->updateStatus();
            if (on) Notification::create("Recording: play to 100% in practice mode", NotificationIcon::Info)->show();
        });
        rec->toggle(isRecording());
        rec->setPosition({leftX + 30.f, top - step * 1.9f});
        menu->addChild(rec);
        m_recordToggle = rec;
        auto recLabel = CCLabelBMFont::create("Recording", "bigFont.fnt");
        recLabel->setAnchorPoint({0.f, 0.5f});
        recLabel->setScale(0.5f);
        recLabel->setPosition({leftX + 50.f, top - step * 1.9f});
        m_pageNode->addChild(recLabel, 10);

        auto saveSpr = ButtonSprite::create("Save Now", "goldFont.fnt", "GJ_button_01.png", 0.6f);
        auto saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr, [this](auto) {
            if (saveRecordingNow()) {
                Notification::create("Click Indicator: macro saved", NotificationIcon::Success)->show();
                this->updateStatus();
            }
            else if (recordedInputCount() == 0) {
                Notification::create("Nothing recorded yet", NotificationIcon::Warning)->show();
            }
        });
        saveBtn->setPosition({rightX + 60.f, top - step * 1.9f});
        menu->addChild(saveBtn);

        this->addLabel(m_pageNode, "Attempt Stats", {kWidth / 2.f, top - step * 3.2f}, 0.5f);
        auto statsSpr = ButtonSprite::create("Show Stats", "goldFont.fnt", "GJ_button_01.png", 0.6f);
        auto statsBtn = CCMenuItemExt::createSpriteExtra(statsSpr, [](auto) {
            if (auto p = StatsPopup::create()) p->show();
        });
        statsBtn->setPosition({kWidth / 2.f - 70.f, top - step * 4.2f});
        menu->addChild(statsBtn);

        auto clearSpr = ButtonSprite::create("Clear Trouble", "goldFont.fnt", "GJ_button_06.png", 0.6f);
        auto clearBtn = CCMenuItemExt::createSpriteExtra(clearSpr, [](auto) {
            clearTroubleForCurrentLevel();
            Notification::create("Trouble spots cleared for this level", NotificationIcon::Info)->show();
        });
        clearBtn->setPosition({kWidth / 2.f + 70.f, top - step * 4.2f});
        menu->addChild(clearBtn);
        this->addToggle(m_pageNode, menu, "Stats On Death", "stats-on-death", {leftX, top - step * 5.3f}, "Shows a short accuracy summary every time you die.");
        this->addToggle(m_pageNode, menu, "Stats On Finish", "stats-on-complete", {rightX, top - step * 5.3f}, "Shows the full stats popup when you complete the level.");
        this->addToggle(m_pageNode, menu, "Merge Recordings", "record-merge", {rightX, top - step * 1.9f + 14.f}, "When a recording is saved over an existing macro with the same name, only the part you recorded is replaced. Record your best sections one at a time.");

        // macro editing row
        this->addLabel(m_pageNode, "Edit Macro", {kWidth / 2.f, top - step * 6.4f + 2.f}, 0.5f);
        auto mkBtn = [&](char const* text, float x, auto cb) {
            auto spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_04.png", 0.5f);
            auto btn = CCMenuItemExt::createSpriteExtra(spr, cb);
            btn->setPosition({x, top - step * 7.3f});
            menu->addChild(btn);
        };
        mkBtn("-5ms", kWidth / 2.f - 150.f, [this](auto) { if (shiftCurrentMacro(-5)) this->updateStatus(); else Notification::create("Open a level with a macro first", NotificationIcon::Info)->show(); });
        mkBtn("+5ms", kWidth / 2.f - 90.f, [this](auto) { if (shiftCurrentMacro(5)) this->updateStatus(); else Notification::create("Open a level with a macro first", NotificationIcon::Info)->show(); });
        mkBtn("Trim Before Here", kWidth / 2.f - 5.f, [this](auto) {
            if (trimCurrentMacroHere()) { Notification::create("Removed clicks before this point", NotificationIcon::Info)->show(); this->updateStatus(); }
            else Notification::create("Open a level with a macro first", NotificationIcon::Info)->show();
        });
        mkBtn("Save Edited", kWidth / 2.f + 85.f, [](auto) {
            auto f = saveEditedMacro();
            if (!f.empty()) Notification::create("Saved " + f, NotificationIcon::Success)->show();
        });
        mkBtn("Export .gdr2", kWidth / 2.f + 160.f, [](auto) {
            auto f = exportCurrentMacroGdr2();
            if (!f.empty()) Notification::create("Exported " + f, NotificationIcon::Success)->show();
        });
    }
    this->updateNumberLabels();
}

void ClickMenu::addLabel(CCNode* parent, char const* text, CCPoint pos, float scale) {
    auto label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setScale(scale);
    label->setAlignment(kCCTextAlignmentCenter);
    label->setPosition(pos);
    parent->addChild(label, 10);
}

void ClickMenu::addToggle(CCNode* parent, CCMenu* menu, char const* text, char const* settingKey, CCPoint pos, char const* info) {
    std::string key = settingKey;
    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.7f, [key](CCMenuItemToggler* t) {
        bool on = !t->isToggled();
        Mod::get()->setSettingValue<bool>(key, on);
        settingChanged();
    });
    toggler->toggle(Mod::get()->getSettingValue<bool>(key));
    toggler->setPosition(pos);
    menu->addChild(toggler);

    auto label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({pos.x + 18.f, pos.y});
    label->limitLabelWidth(120.f, 0.45f, 0.2f);
    parent->addChild(label, 10);

    if (info) {
        std::string title = text;
        std::string desc = info;
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.42f);
        auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [title, desc](auto) {
            FLAlertLayer::create(title.c_str(), desc, "OK")->show();
        });
        infoBtn->setPosition({pos.x - 16.f, pos.y + 11.f});
        menu->addChild(infoBtn);
    }
}

void ClickMenu::addChoiceToggle(CCNode* parent, CCMenu* menu, char const* text, char const* settingKey, char const* onValue, char const* offValue, CCPoint pos, char const* info) {
    std::string key = settingKey, onV = onValue, offV = offValue;
    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.7f, [key, onV, offV](CCMenuItemToggler* t) {
        bool on = !t->isToggled();
        Mod::get()->setSettingValue<std::string>(key, on ? onV : offV);
        settingChanged();
    });
    toggler->toggle(Mod::get()->getSettingValue<std::string>(key) == onV);
    toggler->setPosition(pos);
    menu->addChild(toggler);

    auto label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({pos.x + 18.f, pos.y});
    label->limitLabelWidth(120.f, 0.45f, 0.2f);
    parent->addChild(label, 10);

    if (info) {
        std::string title = text;
        std::string desc = info;
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.42f);
        auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [title, desc](auto) {
            FLAlertLayer::create(title.c_str(), desc, "OK")->show();
        });
        infoBtn->setPosition({pos.x - 16.f, pos.y + 11.f});
        menu->addChild(infoBtn);
    }
}

void ClickMenu::addNumberRow(CCNode* parent, CCMenu* menu, char const* text, char const* settingKey, int64_t lo, int64_t hi, int64_t step, CCPoint pos, CCLabelBMFont** labelOut, char const* suffix) {
    auto title = CCLabelBMFont::create(text, "bigFont.fnt");
    title->setScale(0.38f);
    title->setPosition({pos.x, pos.y + 11.f});
    parent->addChild(title, 10);

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.4f);
    value->setPosition({pos.x, pos.y - 7.f});
    parent->addChild(value, 10);
    if (labelOut) *labelOut = value;

    std::string key = settingKey, suf = suffix;
    bool speedPct = key == "autoplay-speed-pct";
    auto getV = [key, speedPct]() -> int64_t {
        if (speedPct) return static_cast<int64_t>(std::llround(Mod::get()->getSettingValue<double>("autoplay-speed") * 100.0));
        return Mod::get()->getSettingValue<int64_t>(key);
    };
    auto setV = [key, speedPct](int64_t v) {
        if (speedPct) Mod::get()->setSettingValue<double>("autoplay-speed", v / 100.0);
        else Mod::get()->setSettingValue<int64_t>(key, v);
    };
    auto refresh = [key, suf, value, getV]() {
        int64_t v = getV();
        std::string s = fmt::format("{}{}", v, suf);
        if (key == "assist-strength" || (key == "assist-release-strength" && v > 0)) {
            s += fmt::format(" ({:.0f}ms)", ClickOverlay::assistWindowFor(static_cast<int>(v)) * 1000.0);
        }
        else if (key == "assist-release-strength") s = "same as press";
        else if (v == 0 && key != "section-start" && key != "autoplay-speed-pct") s = "never";
        else if (v == 0 && key == "section-start") s = "from start";
        value->setString(s.c_str());
        value->limitLabelWidth(90.f, 0.4f, 0.2f);
    };
    refresh();

    auto makeArrow = [=](bool right) {
        auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        spr->setScale(0.4f);
        (void)lo; (void)hi;
        if (!right) spr->setFlipX(true);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [=](auto) {
            int64_t v = getV() + (right ? step : -step);
            v = std::clamp<int64_t>(v, lo, hi);
            setV(v);
            settingChanged();
            refresh();
        });
        btn->setPosition({pos.x + (right ? 62.f : -62.f), pos.y - 7.f});
        menu->addChild(btn);
    };
    makeArrow(false);
    makeArrow(true);
}

void ClickMenu::updateNumberLabels() {}

void ClickMenu::updateStatus() {
    if (!m_status) return;
    std::string s = macroStatusText();
    if (isRecording()) s += fmt::format("   |   REC {} inputs", recordedInputCount());
    m_status->setString(s.c_str());
    m_status->limitLabelWidth(kWidth - 40.f, 0.7f, 0.3f);
}

void ClickMenu::openMacroPicker() {
    if (!PlayLayer::get()) {
        Notification::create("Open a level first", NotificationIcon::Info)->show();
        return;
    }
    if (auto picker = MacroPicker::create([this]() { this->updateStatus(); })) picker->show();
}

void ClickMenu::onClose(CCObject* sender) {
    if (s_openMenu == this) s_openMenu = nullptr;
    Popup::onClose(sender);
}
