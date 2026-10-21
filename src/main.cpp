#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>

#include "Overlay.hpp"
#include "Menu.hpp"
#include "Shared.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace {

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

std::optional<std::filesystem::path> findMacroFile(GJGameLevel* level) {
    std::error_code ec;
    auto folder = macroFolder();
    std::string id = std::to_string(level->m_levelID.value());
    std::string name = toLower(std::string(level->m_levelName));

    if (std::filesystem::is_directory(folder, ec)) {
        for (auto const& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto stem = macroStem(utils::string::pathToString(entry.path().filename()));
            if (stem == id || (!name.empty() && stem == name)) {
                return entry.path();
            }
        }
    }

    auto file = Mod::get()->getSettingValue<std::filesystem::path>("macro-file");
    if (std::filesystem::is_regular_file(file, ec)) return file;
    return std::nullopt;
}

enumKeyCodes menuKey() {
    auto key = Mod::get()->getSettingValue<std::string>("menu-key");
    if (key == "F8") return KEY_F8;
    if (key == "F9") return KEY_F9;
    if (key == "F10") return KEY_F10;
    if (key == "Tab") return KEY_Tab;
    if (key == "Insert") return KEY_Insert;
    return KEY_None;
}

} // namespace

std::filesystem::path macroFolder() {
    return Mod::get()->getSettingValue<std::filesystem::path>("macro-folder");
}

$on_mod(Loaded) {
    // make sure the macro folder exists so it's easy to find
    std::error_code ec;
    std::filesystem::create_directories(macroFolder(), ec);
}

class $modify(CIPlayLayer, PlayLayer) {
    struct Fields {
        Ref<ClickOverlay> overlay;
        std::string lastError;
        bool forcedTestMode = false;
        bool savedTestMode = false;
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

    // Finds and loads a macro for this level. Replaces the current overlay.
    bool loadMacroOverlay(bool notify) {
        auto fields = m_fields.self();
        if (fields->overlay) {
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

        auto overlay = ClickOverlay::create(std::move(macro));
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

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        this->loadMacroOverlay(true);
        this->updateSafeMode();
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (auto overlay = m_fields->overlay) {
            overlay->refresh(this, dt);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (auto overlay = m_fields->overlay) {
            overlay->onReset();
        }
        this->updateSafeMode();
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

namespace {
    // true while the aim assist is sending a held-back click, so it isn't processed twice
    bool s_assistFiring = false;
}

class $modify(CIBaseGameLayer, GJBaseGameLayer) {
    ClickOverlay* overlayIfPlayLayer() {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return nullptr;
        return static_cast<CIPlayLayer*>(pl)->m_fields->overlay;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto overlay = button == 1 && !s_assistFiring ? this->overlayIfPlayLayer() : nullptr;
        if (!overlay) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto pl = PlayLayer::get();
        bool p2 = !isPlayer1;

        // judge the real click (before any assist), so you see how far off you really were
        overlay->onPlayerInput(pl, down, p2);

        bool holdBack = down ? overlay->assistOnPress(pl, p2) : overlay->assistOnRelease(pl, p2);
        if (!holdBack) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    // Sends held-back assist clicks that are due on this tick
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

    // Right before the game reads its input queue, so a held-back click is applied on this
    // same tick instead of the next one
    void processQueuedButtons(float dt, bool clearInputQueue) {
        this->fireAssist();
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    // Backup in case the queue isn't read on a tick
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

// Hotkey to open the menu from anywhere (pauses the level first)
class $modify(CIKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double time) {
        if (isKeyDown && !isKeyRepeat && key != KEY_None && key == menuKey()) {
            ClickMenu::toggleOpen();
            return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, time);
    }
};
