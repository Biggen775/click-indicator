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
    };

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
    }
};

void refreshOverlaySettings() {
    if (auto pl = PlayLayer::get()) {
        if (auto overlay = static_cast<CIPlayLayer*>(pl)->m_fields->overlay) {
            overlay->loadSettings();
        }
    }
}

bool reloadMacroForCurrentLevel() {
    auto pl = PlayLayer::get();
    if (!pl) return false;
    return static_cast<CIPlayLayer*>(pl)->loadMacroOverlay(true);
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

class $modify(CIBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (button != 1) return; // jump only

        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return;

        if (auto overlay = static_cast<CIPlayLayer*>(pl)->m_fields->overlay) {
            overlay->onPlayerInput(pl, down, !isPlayer1);
        }
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
