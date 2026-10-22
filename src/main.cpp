#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>

#include "Overlay.hpp"
#include "Menu.hpp"
#include "Recorder.hpp"
#include "Shared.hpp"

#include <gdr/gdr.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

using namespace geode::prelude;

namespace {

constexpr double kRecordFramerate = 240.0;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "Bloodbath.gdr.json" -> "bloodbath"
std::string macroStem(std::string name) {
    name = toLower(std::move(name));
    static constexpr std::string_view exts[] = {".json", ".gdr2", ".gdr", ".mhr", ".macro", ".txt"};
    bool stripped = true;
    while (stripped) {
        stripped = false;
        for (auto ext : exts) {
            if (name.size() > ext.size() && name.ends_with(ext)) {
                name.erase(name.size() - ext.size());
                stripped = true;
            }
        }
    }
    return name;
}

bool isMacroFile(std::filesystem::path const& p) {
    auto n = toLower(utils::string::pathToString(p.filename()));
    return n.ends_with(".gdr") || n.ends_with(".gdr2") || n.ends_with(".json") || n.ends_with(".macro");
}

std::string overrideKey(int levelID) {
    return fmt::format("override/{}", levelID);
}

std::optional<std::filesystem::path> findMacroFile(GJGameLevel* level) {
    std::error_code ec;
    auto folder = macroFolder();
    int levelID = level->m_levelID.value();
    std::string id = std::to_string(levelID);
    std::string name = toLower(std::string(level->m_levelName));

    // 1. a macro the player picked for this level in the menu
    auto forced = Mod::get()->getSavedValue<std::string>(overrideKey(levelID), "");
    if (!forced.empty()) {
        auto p = folder / forced;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }

    // 2. a file named after the level
    if (std::filesystem::is_directory(folder, ec)) {
        for (auto const& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto stem = macroStem(utils::string::pathToString(entry.path().filename()));
            if (stem == id || (!name.empty() && stem == name)) {
                return entry.path();
            }
        }
    }

    // 3. the fallback file
    auto file = Mod::get()->getSettingValue<std::filesystem::path>("macro-file");
    if (std::filesystem::is_regular_file(file, ec)) return file;
    return std::nullopt;
}

enumKeyCodes keyFromName(std::string const& key) {
    if (key == "F6") return KEY_F6;
    if (key == "F7") return KEY_F7;
    if (key == "F8") return KEY_F8;
    if (key == "F9") return KEY_F9;
    if (key == "F10") return KEY_F10;
    if (key == "F11") return KEY_F11;
    if (key == "F12") return KEY_F12;
    if (key == "Tab") return KEY_Tab;
    if (key == "Insert") return KEY_Insert;
    if (key == "Home") return KEY_Home;
    if (key == "End") return KEY_End;
    return KEY_None;
}

enumKeyCodes menuKey() { return keyFromName(Mod::get()->getSettingValue<std::string>("menu-key")); }
enumKeyCodes overlayKey() { return keyFromName(Mod::get()->getSettingValue<std::string>("overlay-key")); }
enumKeyCodes assistKey() { return keyFromName(Mod::get()->getSettingValue<std::string>("assist-key")); }

// Settings that "Remember per level" stores
constexpr std::string_view kPerLevelBools[] = {"enabled", "show-bars", "show-hit-line", "show-lane", "show-judgments", "assist-enabled"};
constexpr std::string_view kPerLevelInts[] = {"assist-strength", "assist-release-strength"};

std::string perLevelKey(int levelID) { return fmt::format("per-level/{}", levelID); }

void applyPerLevelSettings(int levelID) {
    if (!Mod::get()->getSettingValue<bool>("per-level")) return;
    auto v = Mod::get()->getSavedValue<matjson::Value>(perLevelKey(levelID), matjson::Value::object());
    if (!v.isObject()) return;
    for (auto key : kPerLevelBools) {
        if (v.contains(key) && v[key].isBool()) Mod::get()->setSettingValue<bool>(key, v[key].asBool().unwrapOr(true));
    }
    for (auto key : kPerLevelInts) {
        if (v.contains(key) && v[key].isNumber()) Mod::get()->setSettingValue<int64_t>(key, v[key].asInt().unwrapOr(5));
    }
}

bool s_recording = false;
Recorder s_recorder;
double s_recordTime = 0.0; // level time at the last reset (to drop inputs after a checkpoint)

} // namespace

std::filesystem::path macroFolder() {
    return Mod::get()->getSettingValue<std::filesystem::path>("macro-folder");
}

$on_mod(Loaded) {
    std::error_code ec;
    std::filesystem::create_directories(macroFolder(), ec);
}

class $modify(CIPlayLayer, PlayLayer) {
    struct Fields {
        Ref<ClickOverlay> overlay;
        std::string lastError;
        bool forcedTestMode = false;
        bool savedTestMode = false;
        bool statsShown = false;
        bool speedApplied = false;
        bool tutorialShown = false;
    };

    // Slow motion while auto-play is on
    void updateSpeed() {
        auto fields = m_fields.self();
        auto mod = Mod::get();
        bool want = fields->overlay && mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("autoplay");
        float speed = static_cast<float>(mod->getSettingValue<double>("autoplay-speed"));
        if (want && speed > 0.05f && speed < 0.999f) {
            CCScheduler::get()->setTimeScale(speed);
            fields->speedApplied = true;
        }
        else if (fields->speedApplied) {
            CCScheduler::get()->setTimeScale(1.f);
            fields->speedApplied = false;
        }
    }

    // Safe mode: while the aim assist is on, run the level like a start-pos test so no progress is saved
    void updateSafeMode() {
        auto fields = m_fields.self();
        auto mod = Mod::get();
        bool want = fields->overlay && mod->getSettingValue<bool>("enabled") &&
            ((mod->getSettingValue<bool>("assist-enabled") && mod->getSettingValue<bool>("assist-safe-mode")) ||
             mod->getSettingValue<bool>("autoplay"));
        if (want && !fields->forcedTestMode) {
            fields->savedTestMode = m_isTestMode;
            fields->forcedTestMode = true;
            m_isTestMode = true;
        }
        else if (!want && fields->forcedTestMode) {
            fields->forcedTestMode = false;
            m_isTestMode = fields->savedTestMode;
        }
    }

    bool loadMacroOverlay(bool notify) {
        auto fields = m_fields.self();
        if (fields->overlay) {
            fields->overlay->saveTrouble();
            fields->overlay->removeFromParent();
            fields->overlay = nullptr;
        }
        fields->lastError.clear();

        auto path = findMacroFile(m_level);
        if (!path) return false;

        auto res = loadMacro(*path);
        if (res.isErr()) {
            fields->lastError = res.unwrapErr();
            log::warn("Failed to load macro {}: {}", utils::string::pathToString(*path), res.unwrapErr());
            if (notify) {
                Notification::create(fmt::format("Click Indicator: {}", res.unwrapErr()), NotificationIcon::Error)->show();
            }
            return false;
        }

        auto macro = std::move(res).unwrap();
        auto fileName = macro.fileName;
        auto clicks = macro.presses.size();

        auto overlay = ClickOverlay::create(std::move(macro), m_level->m_levelID.value());
        if (!overlay) return false;
        this->addChild(overlay, 1000);
        fields->overlay = overlay;

        if (notify && Mod::get()->getSettingValue<bool>("enabled")) {
            Notification::create(
                fmt::format("Click Indicator: {} ({} clicks)", fileName, clicks),
                NotificationIcon::Success
            )->show();
        }
        return true;
    }

    // Recording uses the game's own level clock (what macro bots count frames from),
    // without the user's timing offset, so the file matches other bots' macros.
    double recordClock() {
        return m_gameState.m_levelTime;
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        applyPerLevelSettings(level->m_levelID.value());
        this->loadMacroOverlay(true);
        this->updateSafeMode();
        this->updateSpeed();
        s_recorder.clear();

        if (m_fields->overlay && !Mod::get()->getSavedValue<bool>("tutorial-shown", false)) {
            Mod::get()->setSavedValue<bool>("tutorial-shown", true);
            FLAlertLayer::create(
                "Click Indicator",
                "Welcome! The <cg>green bars</c> in the level are the macro's clicks: a thin line is a tap, a wide bar is a hold. "
                "The <cy>white line</c> is you. The <cg>lane</c> on the side shows upcoming clicks falling toward the receptor; "
                "click when a note reaches it. <cj>PERFECT / GREAT / GOOD / MISS</c> tell you how close you were.\n\n"
                "Press <cy>F8</c> (or the button in the pause menu) for the Click Indicator menu, with Aim Assist, recording, stats and more.",
                "Got it"
            )->show();
        }
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (auto overlay = m_fields->overlay) {
            overlay->refresh(this, dt);

            if (Mod::get()->getSettingValue<bool>("section-practice") && !m_hasCompletedLevel) {
                if (overlay->sectionWantsCheckpoint(this) && m_checkpointArray && m_checkpointArray->count() == 0) {
                    this->markCheckpoint();
                    Notification::create("Section start: checkpoint placed", NotificationIcon::Info, 0.8f)->show();
                }
                if (overlay->sectionEndReached(this)) {
                    overlay->finishAttempt(false);
                    if (Mod::get()->getSettingValue<bool>("stats-on-death") && overlay->getStats().total() > 0) {
                        Notification::create(
                            fmt::format("Section: {:.0f}% accuracy, avg {:.0f} ms off", overlay->getStats().accuracy(), overlay->getStats().avgAbs()),
                            NotificationIcon::Info
                        )->show();
                    }
                    this->resetLevel();
                }
            }
        }
    }

    void resetLevel() {
        auto fields = m_fields.self();
        // stats popup on death (before the reset wipes them)
        if (auto overlay = fields->overlay) {
            if (m_player1 && m_player1->m_isDead) overlay->finishAttempt(false);
            if (!fields->statsShown && m_player1 && m_player1->m_isDead && !m_isPracticeMode &&
                Mod::get()->getSettingValue<bool>("stats-on-death") && overlay->getStats().total() > 0) {
                Notification::create(
                    fmt::format("Attempt: {:.0f}% accuracy, avg {:.0f} ms off", overlay->getStats().accuracy(), overlay->getStats().avgAbs()),
                    NotificationIcon::Info
                )->show();
            }
        }
        fields->statsShown = false;

        PlayLayer::resetLevel();

        if (auto overlay = fields->overlay) overlay->onReset();
        this->updateSafeMode();
        this->updateSpeed();

        // recording: in practice mode keep what happened before the checkpoint, otherwise start over
        if (s_recording) {
            if (m_isPracticeMode && m_checkpointArray && m_checkpointArray->count() > 0) {
                s_recorder.truncate(this->recordClock());
            }
            else {
                s_recorder.clear();
            }
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        auto fields = m_fields.self();

        if (s_recording && !s_recorder.empty()) {
            if (saveRecordingNow()) {
                Notification::create("Click Indicator: macro saved", NotificationIcon::Success)->show();
            }
        }

        if (auto overlay = fields->overlay) {
            overlay->finishAttempt(true);
            overlay->saveTrouble();
            if (Mod::get()->getSettingValue<bool>("stats-on-complete") && overlay->getStats().total() > 0) {
                fields->statsShown = true;
                FLAlertLayer::create("Click Indicator Stats", overlay->statsText(), "OK")->show();
            }
        }
    }

    void onQuit() {
        if (m_fields->speedApplied) CCScheduler::get()->setTimeScale(1.f);
        if (auto overlay = m_fields->overlay) overlay->saveTrouble();
        s_recording = false;
        s_recorder.clear();
        PlayLayer::onQuit();
    }
};

void refreshOverlaySettings() {
    if (auto pl = PlayLayer::get()) {
        if (auto overlay = static_cast<CIPlayLayer*>(pl)->m_fields->overlay) {
            overlay->loadSettings();
        }
        static_cast<CIPlayLayer*>(pl)->updateSafeMode();
        static_cast<CIPlayLayer*>(pl)->updateSpeed();
    }
}

bool reloadMacroForCurrentLevel() {
    auto pl = PlayLayer::get();
    if (!pl) return false;
    bool ok = static_cast<CIPlayLayer*>(pl)->loadMacroOverlay(true);
    static_cast<CIPlayLayer*>(pl)->updateSafeMode();
    return ok;
}

std::string macroStatusText() {
    auto pl = PlayLayer::get();
    if (!pl) return "Open a level to see its macro";
    auto fields = static_cast<CIPlayLayer*>(pl)->m_fields.self();
    if (auto overlay = fields->overlay) {
        auto const& m = overlay->getMacro();
        return fmt::format("Macro: {} ({} clicks)", m.fileName, m.presses.size());
    }
    if (!fields->lastError.empty()) return "Macro error: " + fields->lastError;
    return "No macro found for this level";
}

std::string currentStatsText() {
    auto pl = PlayLayer::get();
    if (!pl) return "Open a level first.";
    if (auto overlay = static_cast<CIPlayLayer*>(pl)->m_fields->overlay) return overlay->statsText();
    return "No macro loaded for this level.";
}

std::vector<std::string> listMacroFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    auto folder = macroFolder();
    if (!std::filesystem::is_directory(folder, ec)) return out;
    for (auto const& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && isMacroFile(entry.path())) {
            out.push_back(utils::string::pathToString(entry.path().filename()));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void useMacroForCurrentLevel(std::string const& fileName) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    Mod::get()->setSavedValue<std::string>(overrideKey(pl->m_level->m_levelID.value()), fileName);
    reloadMacroForCurrentLevel();
}

std::string macroOverrideForCurrentLevel() {
    auto pl = PlayLayer::get();
    if (!pl) return "";
    return Mod::get()->getSavedValue<std::string>(overrideKey(pl->m_level->m_levelID.value()), "");
}

bool isRecording() { return s_recording; }

void setRecording(bool on) {
    s_recording = on;
    s_recorder.clear();
    if (on) {
        // the assist would change the inputs we record
        Mod::get()->setSettingValue<bool>("assist-enabled", false);
        refreshOverlaySettings();
    }
}

int recordedInputCount() { return static_cast<int>(s_recorder.size()); }

bool saveRecordingNow() {
    auto pl = PlayLayer::get();
    if (!pl || s_recorder.empty()) return false;
    std::string name = std::string(pl->m_level->m_levelName);
    if (name.empty()) name = std::to_string(pl->m_level->m_levelID.value());
    for (auto& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    std::error_code ec;
    std::filesystem::create_directories(macroFolder(), ec);
    auto path = macroFolder() / (name + ".gdr.json");

    // Merge: keep the existing macro's clicks outside the recorded range, use the new ones inside it
    if (Mod::get()->getSettingValue<bool>("record-merge") && std::filesystem::is_regular_file(path, ec)) {
        auto existing = loadMacro(path);
        if (existing.isOk()) {
            auto range = s_recorder.timeRange();
            Recorder merged;
            std::vector<Recorder::Input> evs;
            for (auto const& p : existing.unwrap().presses) {
                if (p.end < range.first || p.start > range.second) {
                    evs.push_back({p.start, true, p.p2});
                    evs.push_back({p.end, false, p.p2});
                }
            }
            for (auto const& in : s_recorder.inputs()) evs.push_back(in);
            std::stable_sort(evs.begin(), evs.end(), [](auto const& a, auto const& b) { return a.t < b.t; });
            for (auto const& e : evs) merged.add(e.t, e.down, e.p2);
            auto res = merged.save(path, std::string(pl->m_level->m_levelName), pl->m_level->m_levelID.value(), kRecordFramerate);
            if (res.isErr()) {
                Notification::create("Click Indicator: " + res.unwrapErr(), NotificationIcon::Error)->show();
                return false;
            }
            return true;
        }
    }

    auto res = s_recorder.save(path, std::string(pl->m_level->m_levelName), pl->m_level->m_levelID.value(), kRecordFramerate);
    if (res.isErr()) {
        Notification::create("Click Indicator: " + res.unwrapErr(), NotificationIcon::Error)->show();
        return false;
    }
    return true;
}

bool perLevelEnabled() { return Mod::get()->getSettingValue<bool>("per-level"); }

void savePerLevelSettings() {
    auto pl = PlayLayer::get();
    if (!pl || !perLevelEnabled()) return;
    auto v = matjson::Value::object();
    for (auto key : kPerLevelBools) v.set(key, matjson::Value(Mod::get()->getSettingValue<bool>(key)));
    for (auto key : kPerLevelInts) v.set(key, matjson::Value(Mod::get()->getSettingValue<int64_t>(key)));
    Mod::get()->setSavedValue(perLevelKey(pl->m_level->m_levelID.value()), v);
}

void clearTroubleForCurrentLevel() {
    auto pl = PlayLayer::get();
    if (!pl) return;
    if (auto overlay = static_cast<CIPlayLayer*>(pl)->m_fields->overlay) overlay->resetTrouble();
}

void clearPerLevelSettings() {
    auto pl = PlayLayer::get();
    if (!pl) return;
    Mod::get()->setSavedValue(perLevelKey(pl->m_level->m_levelID.value()), matjson::Value::object());
}

namespace {
    bool s_assistFiring = false;

    ClickOverlay* currentOverlay() {
        auto pl = PlayLayer::get();
        if (!pl) return nullptr;
        return static_cast<CIPlayLayer*>(pl)->m_fields->overlay;
    }

    // Write the overlay's (possibly edited) macro back to a .gdr.json next to the original
    Result<std::filesystem::path> saveMacroAs(Macro const& m, std::string const& suffix, bool gdr2) {
        auto stem = m.fileName;
        for (auto ext : {".json", ".gdr2", ".gdr"}) {
            while (stem.size() > std::strlen(ext) && stem.ends_with(ext)) stem.erase(stem.size() - std::strlen(ext));
        }
        auto path = macroFolder() / (stem + suffix + (gdr2 ? ".gdr2" : ".gdr.json"));

        if (gdr2) {
            gdr::Replay<> r("ClickIndicator", 1);
            r.framerate = m.framerate;
            r.levelInfo = gdr::Level(m.levelName, m.levelID);
            r.platformer = m.platformer;
            r.gameVersion = 22081;
            for (auto const& p : m.presses) {
                r.inputs.push_back({static_cast<uint64_t>(std::llround(std::max(0.0, p.start) * m.framerate)), 1, p.p2, true});
                r.inputs.push_back({static_cast<uint64_t>(std::llround(std::max(0.0, p.end) * m.framerate)), 1, p.p2, false});
            }
            r.sortInputs();
            r.duration = m.presses.empty() ? 0.f : static_cast<float>(m.presses.back().end);
            auto res = r.exportData(path);
            if (res.isErr()) return Err(res.unwrapErr());
            return Ok(path);
        }

        Recorder rec;
        std::vector<std::pair<double, std::pair<bool, bool>>> evs;
        for (auto const& p : m.presses) {
            evs.push_back({std::max(0.0, p.start), {true, p.p2}});
            evs.push_back({std::max(0.0, p.end), {false, p.p2}});
        }
        std::stable_sort(evs.begin(), evs.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (auto const& e : evs) rec.add(e.first, e.second.first, e.second.second);
        auto res = rec.save(path, m.levelName, static_cast<int>(m.levelID), m.framerate);
        if (res.isErr()) return Err(res.unwrapErr());
        return Ok(path);
    }
}

bool shiftCurrentMacro(double ms) {
    auto overlay = currentOverlay();
    if (!overlay) return false;
    overlay->shiftMacro(ms / 1000.0);
    return true;
}

bool trimCurrentMacroHere() {
    auto overlay = currentOverlay();
    if (!overlay) return false;
    overlay->trimMacroBefore(overlay->nowTime());
    return true;
}

std::string saveEditedMacro() {
    auto overlay = currentOverlay();
    if (!overlay) return "";
    auto res = saveMacroAs(overlay->getMacro(), " (edited)", false);
    if (res.isErr()) {
        Notification::create("Click Indicator: " + res.unwrapErr(), NotificationIcon::Error)->show();
        return "";
    }
    return utils::string::pathToString(res.unwrap().filename());
}

std::string exportCurrentMacroGdr2() {
    auto overlay = currentOverlay();
    if (!overlay) return "";
    auto res = saveMacroAs(overlay->getMacro(), "", true);
    if (res.isErr()) {
        Notification::create("Click Indicator: " + res.unwrapErr(), NotificationIcon::Error)->show();
        return "";
    }
    return utils::string::pathToString(res.unwrap().filename());
}

std::vector<double> attemptHistory() {
    auto overlay = currentOverlay();
    if (!overlay) return {};
    return overlay->history();
}

class $modify(CIBaseGameLayer, GJBaseGameLayer) {
    ClickOverlay* overlayIfPlayLayer() {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return nullptr;
        return static_cast<CIPlayLayer*>(pl)->m_fields->overlay;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        bool inPlayLayer = pl && static_cast<GJBaseGameLayer*>(pl) == this;

        if (button == 1 && !s_assistFiring && inPlayLayer && s_recording) {
            s_recorder.add(static_cast<CIPlayLayer*>(pl)->recordClock(), down, !isPlayer1);
        }

        auto overlay = button == 1 && !s_assistFiring && inPlayLayer ? static_cast<CIPlayLayer*>(pl)->m_fields->overlay.data() : nullptr;
        if (!overlay) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        bool p2 = !isPlayer1;
        if (overlay->autoplayActive(pl)) return; // the bot is playing: ignore the real input
        overlay->onPlayerInput(pl, down, p2);

        overlay->setFire([this](bool d, bool pp2) {
            s_assistFiring = true;
            this->handleButton(d, 1, !pp2);
            s_assistFiring = false;
        });
        bool handled = down ? overlay->assistOnPress(pl, p2) : overlay->assistOnRelease(pl, p2);
        if (!handled) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    void fireAssist() {
        if (s_assistFiring) return;
        if (auto overlay = this->overlayIfPlayLayer()) {
            auto fire = [this](bool down, bool p2) {
                s_assistFiring = true;
                this->handleButton(down, 1, !p2);
                s_assistFiring = false;
            };
            overlay->autoplayTick(PlayLayer::get(), fire);
            overlay->assistTick(PlayLayer::get(), fire);
        }
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        this->fireAssist();
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        this->fireAssist();
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

// Button in the pause menu, under the options gear
class $modify(CIPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();

        auto icon = CCSprite::create("menu-icon.png"_spr);
        auto spr = CircleButtonSprite::create(icon, CircleBaseColor::Green, CircleBaseSize::Small);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](auto) {
            ClickMenu::toggleOpen();
        });
        btn->setID("click-indicator-button"_spr);

        auto menu = CCMenu::create();
        menu->setID("click-indicator-menu"_spr);
        menu->setPosition({win.width - 30.f, win.height - 78.f});
        menu->addChild(btn);
        this->addChild(menu, 10);
    }
};

// Hotkeys
class $modify(CIKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double time) {
        if (isKeyDown && !isKeyRepeat && key != KEY_None) {
            if (key == menuKey()) {
                ClickMenu::toggleOpen();
                return true;
            }
            if (key == overlayKey()) {
                bool on = !Mod::get()->getSettingValue<bool>("enabled");
                Mod::get()->setSettingValue<bool>("enabled", on);
                refreshOverlaySettings();
                savePerLevelSettings();
                Notification::create(on ? "Click Indicator: ON" : "Click Indicator: OFF", NotificationIcon::Info, 0.8f)->show();
                return true;
            }
            if (key == assistKey()) {
                bool on = !Mod::get()->getSettingValue<bool>("assist-enabled");
                Mod::get()->setSettingValue<bool>("assist-enabled", on);
                refreshOverlaySettings();
                savePerLevelSettings();
                Notification::create(on ? "Aim Assist: ON" : "Aim Assist: OFF", NotificationIcon::Info, 0.8f)->show();
                return true;
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, time);
    }
};
