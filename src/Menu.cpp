#include "Menu.hpp"
#include "Shared.hpp"
#include "Overlay.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    ClickMenu* s_openMenu = nullptr;

    constexpr float kWidth = 440.f;
    constexpr float kHeight = 300.f;
    constexpr int kPages = 5;
    constexpr char const* kPageNames[kPages] = {"Display", "Lane & Sound", "Aim Assist", "Practice", "Tools"};

    void settingChanged() {
        refreshOverlaySettings();
        savePerLevelSettings();
        setDiagEnabled(Mod::get()->getSettingValue<bool>("diag-log"));
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
        btn->setPosition({kWidth / 2.f + (right ? 100.f : -100.f), kHeight - 44.f});
        m_menu->addChild(btn);
    };
    makeArrow(false);
    makeArrow(true);

    // status line + bottom buttons (shared by all pages)
    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({kWidth / 2.f, 52.f});
    m_mainLayer->addChild(m_status, 10);

    auto reloadSpr = ButtonSprite::create("Reload", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto reloadBtn = CCMenuItemExt::createSpriteExtra(reloadSpr, [this](auto) {
        bool ok = reloadMacroForCurrentLevel();
        this->updateStatus();
        if (!PlayLayer::get()) Notification::create("Open a level to load its macro", NotificationIcon::Info)->show();
        else if (!ok) Notification::create("No macro found for this level", NotificationIcon::Warning)->show();
    });
    reloadBtn->setPosition({kWidth / 2.f - 160.f, 24.f});
    m_menu->addChild(reloadBtn);

    auto pickSpr = ButtonSprite::create("Pick Macro", "goldFont.fnt", "GJ_button_01.png", 0.65f);
    auto pickBtn = CCMenuItemExt::createSpriteExtra(pickSpr, [this](auto) { this->openMacroPicker(); });
    pickBtn->setPosition({kWidth / 2.f - 55.f, 24.f});
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
    moreBtn->setPosition({kWidth / 2.f + 160.f, 24.f});
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

    // Grid: two columns, items stacked from the top with fixed heights so nothing overlaps.
    constexpr float kTop = kHeight - 72.f;   // first row centre
    constexpr float kToggleH = 25.f;
    constexpr float kNumberH = 36.f;
    constexpr float kLeftX = 30.f;
    constexpr float kRightX = kWidth / 2.f + 18.f;
    constexpr float kNumOffset = 72.f;       // number rows are centred this far right of the column edge

    struct Column {
        ClickMenu* m; CCNode* node; CCMenu* menu; float x; float y;
        void toggle(char const* text, char const* key, char const* info) {
            m->addToggle(node, menu, text, key, {x, y}, info); y -= kToggleH;
        }
        void choice(char const* text, char const* key, char const* on, char const* off, char const* info) {
            m->addChoiceToggle(node, menu, text, key, on, off, {x, y}, info); y -= kToggleH;
        }
        void number(char const* text, char const* key, int64_t lo, int64_t hi, int64_t step, char const* suffix, CCLabelBMFont** out = nullptr) {
            m->addNumberRow(node, menu, text, key, lo, hi, step, {x + kNumOffset, y - 6.f}, out, suffix); y -= kNumberH;
        }
        void label(char const* text, float scale = 0.4f) {
            m->addLabel(node, text, {x + kNumOffset, y}, scale); y -= 16.f;
        }
        void gap(float h) { y -= h; }
        void button(char const* text, geode::Function<void(CCMenuItemSpriteExtra*)> cb, float scale = 0.55f) {
            auto spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", scale);
            auto btn = CCMenuItemExt::createSpriteExtra(spr, std::move(cb));
            btn->setPosition({x + kNumOffset, y});
            menu->addChild(btn);
            y -= 28.f;
        }
    };
    Column L{this, m_pageNode, menu, kLeftX, kTop};
    Column R{this, m_pageNode, menu, kRightX, kTop};

    if (m_page == 0) { // Display
        L.toggle("Enabled", "enabled", "Turns the whole click indicator on or off. Also toggled by the Overlay hotkey (F7 by default).");
        L.toggle("Click Bars", "show-bars", "Green bars in the level where each click happens. Thin line = tap, wide bar = hold.");
        L.toggle("Hit Line", "show-hit-line", nullptr);
        L.toggle("Click Markers", "show-markers", nullptr);
        L.toggle("Hide Passed", "hide-passed", "Only shows clicks that are still ahead of you.");
        L.toggle("Trouble Spots", "trouble-spots", "Clicks you keep getting wrong (late, early or missed) across attempts turn red so you know what to practice. Hitting them well again clears the red.");

        R.toggle("Judgments", "show-judgments", "Shows PERFECT / GREAT / GOOD / MISS and how many ms early or late you clicked.");
        R.toggle("Offset Trail", "offset-trail", "A dot on each lane note shows how early or late you were: yellow = this attempt, white = last attempt.");
        R.toggle("Timeline", "timeline", "A thin bar across the top with every click as a tick mark (red = trouble spot), your position, and the section markers.");
        R.toggle("Practice Only", "only-practice", "Only show the indicator (and let Aim Assist act) in practice mode or from a start pos. If Aim Assist seems to do nothing, make sure this is off or you are practicing.");
        R.number("Hide Overlay At %", "hide-after-percent", 0, 100, 5, "%");
        R.toggle("Per-Level Settings", "per-level", "Remembers the settings on these pages separately for every level.");
    }
    else if (m_page == 1) { // Lane & Sound
        L.toggle("Rhythm Lane", "show-lane", "The osu!mania-style lane with falling notes.");
        L.choice("Horizontal Lane", "lane-style", "Horizontal", "Vertical", "Guitar Hero style: notes come in from the right along the bottom of the screen instead of a vertical tower.");
        L.choice("Lane On Right", "lane-side", "Right", "Left", nullptr);
        L.toggle("Next-Click Countdown", "countdown", "Seconds until the next click, shown near the lane when the gap is 1 second or more.");
        L.toggle("Click Sounds", "tick-sound", "Plays a tick at every macro click so you can learn the rhythm by ear. Settings has a volume slider and a separate tick for releases.");
        L.toggle("Count-In Beats", "count-in", "Three soft ticks half a second apart before the first click after a restart or checkpoint.");

        R.toggle("Pre-Click Cue", "cue-sound", "Plays a heads-up sound a little BEFORE each click, so you know one is coming. Pick the sound below (it previews). How early and how loud are in Settings (Cue Lead, Cue Volume). Choose Custom and pick your own .ogg/.mp3/.wav in Settings.");
        R.gap(6.f);
        this->addCueSoundRow(m_pageNode, menu, {R.x + kNumOffset, R.y - 6.f});
        R.gap(kNumberH);
        R.number("Lane Look-Ahead", "lane-lookahead-ds", 3, 40, 1, "");
    }
    else if (m_page == 2) { // Aim Assist
        L.toggle("Aim Assist", "assist-enabled", "If you click EARLY, your click is held back and sent at the exact moment the macro clicked. Late clicks go through normally. At release strength 5 and up, letting go early does NOT end the hold. Also toggled by the Assist hotkey (F9 by default).");
        L.toggle("Assist Flash", "assist-flash", "Flashes the lane receptor yellow and shows ASSIST whenever a click was corrected.");
        L.toggle("Safe Mode", "assist-safe-mode", "While the assist is on, the level runs like a start-pos test so no percentage or completion is saved.");
        L.toggle("Progressive Assist", "progressive-assist", "Every time you finish a run or section with no misses and at least the target accuracy, the assist strength drops by one.");
        L.number("Clean Run Target %", "progressive-target", 50, 100, 5, "%");

        R.number("Press Strength", "assist-strength", 1, 10, 1, "", &m_strengthLabel);
        R.number("Release Strength", "assist-release-strength", 0, 10, 1, "", &m_releaseLabel);
        R.number("Assist Off At %", "assist-off-percent", 0, 100, 5, "%");
        R.number("Release Offset (ms)", "release-offset-ms", -50, 80, 2, "ms");
    }
    else if (m_page == 3) { // Practice
        L.toggle("Auto-Play", "autoplay", "The mod plays the macro for you so you can watch the clicks and the lane in sync. Your own clicks are ignored while it's on. Progress is never saved in auto-play.");
        L.number("Auto-Play Speed %", "autoplay-speed-pct", 25, 100, 5, "%");
        L.toggle("Section Practice", "section-practice", "Drill one part of the level. In practice mode a checkpoint is placed when you reach the start percent, and the level restarts when you reach the end percent. Stats count the section.");
        L.number("Section Start %", "section-start", 0, 99, 1, "%");
        L.number("Section End %", "section-end", 0, 100, 1, "%");

        R.toggle("Absorb Spam In Holds", "spam-assist", "Extra taps while the macro is holding a long hold (120 ms+) are absorbed, so the game keeps holding.");
        R.toggle("Follow While Held", "spam-follow", "While you keep holding, the game follows the macro's press/release pattern for up to Max Hold. Strong: holding through a spam section plays the spam. Off by default.");
        R.toggle("Swallow Stray Taps", "spam-swallow", "Taps that match nothing in the macro (nothing within 150 ms) are not sent to the game. Off by default.");
        R.number("Follow Max Hold (s)", "spam-follow-max-ds", 5, 50, 5, "");
        R.number("Hold Slack (ms)", "hold-slack-ms", 0, 150, 5, "ms");
    }
    else { // Tools
        L.toggle("Stats On Death", "stats-on-death", "Shows a short accuracy summary every time you die.");
        L.toggle("Stats On Finish", "stats-on-complete", "Shows the full stats popup when you complete the level.");
        L.toggle("Merge Recordings", "record-merge", "When a recording is saved over an existing macro with the same name, only the part you recorded is replaced. Record your best sections one at a time.");
        L.choice("Macro From Start Pos", "macro-start", "Start Pos", "Level Start", "Eclipse saves a macro recorded from a start pos with time counted from that start pos, so its clicks show up at the start of the level. ON: the macro's time 0 is the start pos you play from. OFF: time 0 is the level start. The default (Auto, in Settings) guesses: a macro that begins right away and is shorter than the rest of the level is treated as a start-pos recording.");
        // recording toggle (not a setting)
        {
            auto rec = CCMenuItemExt::createTogglerWithStandardSprites(0.7f, [this](CCMenuItemToggler* t) {
                bool on = !t->isToggled();
                setRecording(on);
                this->updateStatus();
                if (on) Notification::create("Recording: play to 100% in practice mode", NotificationIcon::Info)->show();
            });
            rec->toggle(isRecording());
            rec->setPosition({L.x, L.y});
            menu->addChild(rec);
            m_recordToggle = rec;
            auto recLabel = CCLabelBMFont::create("Record Macro", "bigFont.fnt");
            recLabel->setAnchorPoint({0.f, 0.5f});
            recLabel->setPosition({L.x + 18.f, L.y});
            recLabel->limitLabelWidth(120.f, 0.45f, 0.2f);
            m_pageNode->addChild(recLabel, 10);
            auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
            infoSpr->setScale(0.42f);
            auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [](auto) {
                FLAlertLayer::create("Record Macro", "Turn on, play in practice mode to 100% and the macro saves automatically, named after the level. Dying keeps the clicks from before the checkpoint. Save Now writes what you have so far.", "OK")->show();
            });
            infoBtn->setPosition({L.x - 16.f, L.y + 11.f});
            menu->addChild(infoBtn);
            L.y -= kToggleH;
        }

        R.button("Save Recording", [this](auto) {
            if (saveRecordingNow()) { Notification::create("Click Indicator: macro saved", NotificationIcon::Success)->show(); this->updateStatus(); }
            else if (recordedInputCount() == 0) Notification::create("Nothing recorded yet", NotificationIcon::Warning)->show();
        });
        R.button("Show Stats", [](auto) { if (auto p = StatsPopup::create()) p->show(); });
        R.button("Clear Trouble Spots", [](auto) {
            clearTroubleForCurrentLevel();
            Notification::create("Trouble spots cleared for this level", NotificationIcon::Info)->show();
        });
        R.button("Reset Level Prefs", [](auto) {
            clearPerLevelSettings();
            Notification::create("Per-level settings cleared for this level", NotificationIcon::Info)->show();
        });

        // diagnostics
        L.toggle("Diagnostic Log", "diag-log", "Writes diag.log (every click, every assisted click, positions, clocks, deaths) so timing problems can be read off a file. Turn on, play the problem spot, then use Open Log Folder and send the file.");
        R.button("Open Log Folder", [](auto) {
            std::error_code ec;
            std::filesystem::create_directories(diagPath().parent_path(), ec);
            utils::file::openFolder(diagPath().parent_path());
        });

        // macro editing row along the bottom of the content area
        float ey = 76.f;
        auto title = CCLabelBMFont::create("Edit Macro:", "bigFont.fnt");
        title->setScale(0.38f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({kLeftX - 8.f, ey});
        m_pageNode->addChild(title, 10);
        auto mkBtn = [&](char const* text, float x, auto cb) {
            auto spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_04.png", 0.45f);
            auto btn = CCMenuItemExt::createSpriteExtra(spr, cb);
            btn->setPosition({x, ey});
            menu->addChild(btn);
        };
        mkBtn("-5ms", 120.f, [this](auto) { if (shiftCurrentMacro(-5)) this->updateStatus(); else Notification::create("Open a level with a macro first", NotificationIcon::Info)->show(); });
        mkBtn("+5ms", 168.f, [this](auto) { if (shiftCurrentMacro(5)) this->updateStatus(); else Notification::create("Open a level with a macro first", NotificationIcon::Info)->show(); });
        mkBtn("Trim Before", 236.f, [this](auto) {
            if (trimCurrentMacroHere()) { Notification::create("Removed clicks before this point", NotificationIcon::Info)->show(); this->updateStatus(); }
            else Notification::create("Open a level with a macro first", NotificationIcon::Info)->show();
        });
        mkBtn("Save Edited", 316.f, [](auto) {
            auto f = saveEditedMacro();
            if (!f.empty()) Notification::create("Saved " + f, NotificationIcon::Success)->show();
        });
        mkBtn("To .gdr2", 388.f, [](auto) {
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
    label->limitLabelWidth(140.f, 0.42f, 0.2f);
    parent->addChild(label, 10);

    if (info) {
        std::string title = text;
        std::string desc = info;
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.42f);
        auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [title, desc](auto) {
            FLAlertLayer::create(title.c_str(), desc, "OK")->show();
        });
        infoBtn->setPosition({pos.x - 16.f, pos.y + 10.f});
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
    label->limitLabelWidth(140.f, 0.42f, 0.2f);
    parent->addChild(label, 10);

    if (info) {
        std::string title = text;
        std::string desc = info;
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.42f);
        auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [title, desc](auto) {
            FLAlertLayer::create(title.c_str(), desc, "OK")->show();
        });
        infoBtn->setPosition({pos.x - 16.f, pos.y + 10.f});
        menu->addChild(infoBtn);
    }
}

void ClickMenu::addNumberRow(CCNode* parent, CCMenu* menu, char const* text, char const* settingKey, int64_t lo, int64_t hi, int64_t step, CCPoint pos, CCLabelBMFont** labelOut, char const* suffix) {
    auto title = CCLabelBMFont::create(text, "bigFont.fnt");
    title->setScale(0.36f);
    title->limitLabelWidth(130.f, 0.36f, 0.2f);
    title->setPosition({pos.x, pos.y + 10.f});
    parent->addChild(title, 10);

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.38f);
    value->setPosition({pos.x, pos.y - 7.f});
    parent->addChild(value, 10);
    if (labelOut) *labelOut = value;

    std::string key = settingKey, suf = suffix;
    bool speedPct = key == "autoplay-speed-pct";
    bool followDs = key == "spam-follow-max-ds";
    bool isFloat = key == "release-offset-ms" || key == "hold-slack-ms";
    bool lookDs = key == "lane-lookahead-ds";
    auto getV = [key, speedPct, followDs, isFloat, lookDs]() -> int64_t {
        if (speedPct) return static_cast<int64_t>(std::llround(Mod::get()->getSettingValue<double>("autoplay-speed") * 100.0));
        if (followDs) return static_cast<int64_t>(std::llround(Mod::get()->getSettingValue<double>("spam-follow-max") * 10.0));
        if (isFloat) return static_cast<int64_t>(std::llround(Mod::get()->getSettingValue<double>(key)));
        if (lookDs) return static_cast<int64_t>(std::llround(Mod::get()->getSettingValue<double>("lane-lookahead") * 10.0));
        return Mod::get()->getSettingValue<int64_t>(key);
    };
    auto setV = [key, speedPct, followDs, isFloat, lookDs](int64_t v) {
        if (speedPct) Mod::get()->setSettingValue<double>("autoplay-speed", v / 100.0);
        else if (followDs) Mod::get()->setSettingValue<double>("spam-follow-max", v / 10.0);
        else if (isFloat) Mod::get()->setSettingValue<double>(key, static_cast<double>(v));
        else if (lookDs) Mod::get()->setSettingValue<double>("lane-lookahead", v / 10.0);
        else Mod::get()->setSettingValue<int64_t>(key, v);
    };
    auto refresh = [key, suf, value, getV]() {
        int64_t v = getV();
        std::string s = fmt::format("{}{}", v, suf);
        if (key == "assist-strength" || (key == "assist-release-strength" && v > 0)) {
            s += fmt::format(" ({:.0f}ms)", ClickOverlay::assistWindowFor(static_cast<int>(v)) * 1000.0);
        }
        else if (key == "spam-follow-max-ds" || key == "lane-lookahead-ds") s = fmt::format("{:.1f} s", v / 10.0);
        else if (key == "release-offset-ms") s = fmt::format("{:+d} ms", static_cast<int>(v));
        else if (key == "assist-release-strength") s = "same as press";
        else if (v == 0 && key != "section-start" && key != "autoplay-speed-pct") s = "never";
        else if (v == 0 && key == "section-start") s = "from start";
        value->setString(s.c_str());
        value->limitLabelWidth(84.f, 0.38f, 0.2f);
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

void ClickMenu::addCueSoundRow(CCNode* parent, CCMenu* menu, CCPoint pos) {
    static constexpr char const* kSounds[] = {"Beep", "High Beep", "Low Tone", "Click", "Buzz", "Double Beep", "Ping", "Custom"};
    constexpr int kCount = 8;

    auto title = CCLabelBMFont::create("Cue Sound", "bigFont.fnt");
    title->setScale(0.36f);
    title->limitLabelWidth(130.f, 0.36f, 0.2f);
    title->setPosition({pos.x, pos.y + 10.f});
    parent->addChild(title, 10);

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.38f);
    value->setPosition({pos.x, pos.y - 7.f});
    parent->addChild(value, 10);

    auto refresh = [value]() {
        value->setString(Mod::get()->getSettingValue<std::string>("cue-sound-type").c_str());
        value->limitLabelWidth(84.f, 0.38f, 0.2f);
    };
    refresh();

    auto preview = []() {
        // play the chosen sound once so you can hear it
        auto name = Mod::get()->getSettingValue<std::string>("cue-sound-type");
        std::string file = "cue-beep.ogg";
        if (name == "High Beep") file = "cue-high.ogg";
        else if (name == "Low Tone") file = "cue-low.ogg";
        else if (name == "Click") file = "cue-click.ogg";
        else if (name == "Buzz") file = "cue-buzz.ogg";
        else if (name == "Double Beep") file = "cue-double.ogg";
        else if (name == "Ping") file = "cue-ping.ogg";
        std::string path = utils::string::pathToString(Mod::get()->getResourcesDir() / file);
        if (name == "Custom") {
            auto custom = Mod::get()->getSettingValue<std::filesystem::path>("cue-custom-file");
            std::error_code ec;
            if (std::filesystem::is_regular_file(custom, ec)) path = utils::string::pathToString(custom);
        }
        FMODAudioEngine::sharedEngine()->playEffect(path, 1.f, 0.f, static_cast<float>(Mod::get()->getSettingValue<double>("cue-volume")));
    };

    auto makeArrow = [=](bool right) {
        auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        spr->setScale(0.4f);
        if (!right) spr->setFlipX(true);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [=](auto) {
            auto cur = Mod::get()->getSettingValue<std::string>("cue-sound-type");
            int idx = 0;
            for (int i = 0; i < kCount; i++) if (cur == kSounds[i]) idx = i;
            idx = (idx + (right ? 1 : kCount - 1)) % kCount;
            Mod::get()->setSettingValue<std::string>("cue-sound-type", kSounds[idx]);
            settingChanged();
            refresh();
            preview();
        });
        btn->setPosition({pos.x + (right ? 58.f : -58.f), pos.y - 7.f});
        menu->addChild(btn);
    };
    makeArrow(false);
    makeArrow(true);

    auto playSpr = CCSprite::createWithSpriteFrameName("GJ_playMusicBtn_001.png");
    playSpr->setScale(0.45f);
    auto playBtn = CCMenuItemExt::createSpriteExtra(playSpr, [=](auto) { preview(); });
    playBtn->setPosition({pos.x + 84.f, pos.y - 7.f});
    menu->addChild(playBtn);
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
