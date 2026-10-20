#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include "Overlay.hpp"

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

std::filesystem::path macroFolder() {
    return Mod::get()->getSettingValue<std::filesystem::path>("macro-folder");
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

} // namespace

$on_mod(Loaded) {
    // make sure the macro folder exists so it's easy to find
    std::error_code ec;
    std::filesystem::create_directories(macroFolder(), ec);
}

class $modify(CIPlayLayer, PlayLayer) {
    struct Fields {
        Ref<ClickOverlay> overlay;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!Mod::get()->getSettingValue<bool>("enabled")) return true;

        auto path = findMacroFile(level);
        if (!path) return true;

        auto res = loadMacro(*path);
        if (res.isErr()) {
            log::warn("Failed to load macro {}: {}", utils::string::pathToString(*path), res.unwrapErr());
            Notification::create(fmt::format("Click Indicator: {}", res.unwrapErr()), NotificationIcon::Error)->show();
            return true;
        }

        auto macro = std::move(res).unwrap();
        auto fileName = macro.fileName;
        auto clicks = macro.presses.size();

        auto overlay = ClickOverlay::create(std::move(macro));
        if (!overlay) return true;
        this->addChild(overlay, 1000);
        m_fields->overlay = overlay;

        Notification::create(
            fmt::format("Click Indicator: {} ({} clicks)", fileName, clicks),
            NotificationIcon::Success
        )->show();
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
