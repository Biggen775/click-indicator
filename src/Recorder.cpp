#include "Recorder.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>

#include <cmath>

using namespace geode;

Result<> Recorder::save(std::filesystem::path const& path, std::string const& levelName, int levelID, double framerate) const {
    if (m_inputs.empty()) return Err("Nothing recorded yet");

    auto root = matjson::Value::object();
    root.set("gameVersion", matjson::Value(static_cast<std::intmax_t>(22081)));
    root.set("description", matjson::Value("Recorded with Click Indicator"));
    root.set("version", matjson::Value(static_cast<std::intmax_t>(1)));
    root.set("duration", matjson::Value(m_inputs.back().t));
    auto bot = matjson::Value::object();
    bot.set("name", matjson::Value("ClickIndicator"));
    bot.set("version", matjson::Value(static_cast<std::intmax_t>(1)));
    root.set("bot", bot);
    auto level = matjson::Value::object();
    level.set("id", matjson::Value(static_cast<std::intmax_t>(levelID)));
    level.set("name", matjson::Value(levelName));
    root.set("level", level);
    root.set("author", matjson::Value("you"));
    root.set("seed", matjson::Value(static_cast<std::intmax_t>(0)));
    root.set("coins", matjson::Value(static_cast<std::intmax_t>(0)));
    root.set("ldm", matjson::Value(false));
    root.set("framerate", matjson::Value(framerate));

    auto inputs = matjson::Value::array();
    bool held[2] = {false, false};
    for (auto const& in : m_inputs) {
        auto j = matjson::Value::object();
        j.set("frame", matjson::Value(static_cast<std::intmax_t>(std::llround(in.t * framerate))));
        j.set("btn", matjson::Value(static_cast<std::intmax_t>(1)));
        j.set("2p", matjson::Value(in.p2));
        j.set("down", matjson::Value(in.down));
        inputs.push(j);
        held[in.p2 ? 1 : 0] = in.down;
    }
    // close any hold that is still open
    for (int p = 0; p < 2; p++) {
        if (!held[p]) continue;
        auto j = matjson::Value::object();
        j.set("frame", matjson::Value(static_cast<std::intmax_t>(std::llround(m_inputs.back().t * framerate) + 10)));
        j.set("btn", matjson::Value(static_cast<std::intmax_t>(1)));
        j.set("2p", matjson::Value(p == 1));
        j.set("down", matjson::Value(false));
        inputs.push(j);
    }
    root.set("inputs", inputs);

    return utils::file::writeStringSafe(path, root.dump(2));
}
