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

#include <algorithm>
#include <cctype>

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
    };

    // Safe mode: while the aim assist is on, run the level like a start-pos test so no progress is saved
    void updateSafeMode() {
        auto fields = m_fields.self();
        auto mod = Mod::get();
        bool want = fields->overlay && mod->getSettingValue<bool>("enabled") &&
            mod->getSettingValue<bool>("assist-enabled") && mod->getSettingValue<bool>("assist-safe-mode");
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

    double recordClock() {
        if (auto overlay = m_fields->overlay) return overlay->currentTime(this);
        return m_gameState.m_levelTime;
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        applyPerLevelSettings(level->m_levelID.value());
        this->loadMacroOverlay(true);
        this->updateSafeMode();
        s_recorder.clear();
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (auto overlay = m_fields->overlay) {
            overlay->refresh(this, dt);
        }
    }

    void resetLevel() {
        auto fields = m_fields.self();
        // stats popup on death (before the reset wipes them)
        if (auto overlay = fields->overlay) {
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
            overlay->saveTrouble();
            if (Mod::get()->getSettingValue<bool>("stats-on-complete") && overlay->getStats().total() > 0) {
                fields->statsShown = true;
                FLAlertLayer::create("Click Indicator Stats", overlay->statsText(), "OK")->show();
            }
        }
    }

    void onQuit() {
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
        overlay->onPlayerInput(pl, down, p2);

        bool holdBack = down ? overlay->assistOnPress(pl, p2) : overlay->assistOnRelease(pl, p2);
        if (!holdBack) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    void fireAssist() {
        if (s_assistFiring) return;
        if (auto overlay = this->overlayIfPlayLayer()) {
            overlay->assistTick(PlayLayer::get(), [this](bool down, bool p2) {
                s_assistFiring = true;
                this->handleButton(down, 1, !p2);
                s_assistFiring = false;
            });
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
