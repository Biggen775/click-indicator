#include "Macro.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <gdr/gdr.hpp>

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <span>

using namespace geode;

namespace {

struct RawInput {
    uint64_t frame = 0;
    double time = -1.0; // exact level-clock time in seconds when the file has one (our own recordings)
    int button = 1;
    bool p2 = false;
    bool down = false;
};

struct RawMacro {
    double framerate = 240.0;
    bool platformer = false;
    // Click Indicator v1 files (before v2.3.5) rounded frame = round(levelTime * fps), i.e. frame F
    // is the tick at F / fps, not (F - 1) / fps like the bots. They carry no exact times.
    bool legacyRounding = false;
    uint32_t levelID = 0;
    std::string levelName;
    std::vector<RawInput> inputs;
};

// ---------------------------------------------------------------------------
// Minimal MessagePack -> matjson decoder (GDR v1 ".gdr" files are msgpack)
// ---------------------------------------------------------------------------
class MsgpackReader {
public:
    explicit MsgpackReader(std::span<uint8_t const> data) : m_data(data) {}

    Result<matjson::Value> read(int depth = 0) {
        if (depth > 64) return Err("msgpack: nesting too deep");
        uint8_t b;
        GEODE_UNWRAP_INTO(b, this->u8());

        if (b <= 0x7f) return Ok(matjson::Value(static_cast<std::intmax_t>(b)));
        if (b >= 0xe0) return Ok(matjson::Value(static_cast<std::intmax_t>(static_cast<int8_t>(b))));
        if ((b & 0xf0) == 0x80) return this->map(b & 0x0f, depth);
        if ((b & 0xf0) == 0x90) return this->array(b & 0x0f, depth);
        if ((b & 0xe0) == 0xa0) return this->str(b & 0x1f);

        switch (b) {
            case 0xc0: return Ok(matjson::Value(nullptr));
            case 0xc2: return Ok(matjson::Value(false));
            case 0xc3: return Ok(matjson::Value(true));
            case 0xc4: { GEODE_UNWRAP_INTO(auto n, this->be(1)); GEODE_UNWRAP(this->skip(n)); return Ok(matjson::Value(nullptr)); }
            case 0xc5: { GEODE_UNWRAP_INTO(auto n, this->be(2)); GEODE_UNWRAP(this->skip(n)); return Ok(matjson::Value(nullptr)); }
            case 0xc6: { GEODE_UNWRAP_INTO(auto n, this->be(4)); GEODE_UNWRAP(this->skip(n)); return Ok(matjson::Value(nullptr)); }
            case 0xc7: { GEODE_UNWRAP_INTO(auto n, this->be(1)); GEODE_UNWRAP(this->skip(n + 1)); return Ok(matjson::Value(nullptr)); }
            case 0xc8: { GEODE_UNWRAP_INTO(auto n, this->be(2)); GEODE_UNWRAP(this->skip(n + 1)); return Ok(matjson::Value(nullptr)); }
            case 0xc9: { GEODE_UNWRAP_INTO(auto n, this->be(4)); GEODE_UNWRAP(this->skip(n + 1)); return Ok(matjson::Value(nullptr)); }
            case 0xca: {
                GEODE_UNWRAP_INTO(auto v, this->be(4));
                uint32_t bits = static_cast<uint32_t>(v);
                float f; std::memcpy(&f, &bits, 4);
                return Ok(matjson::Value(static_cast<double>(f)));
            }
            case 0xcb: {
                GEODE_UNWRAP_INTO(auto v, this->be(8));
                double d; std::memcpy(&d, &v, 8);
                return Ok(matjson::Value(d));
            }
            case 0xcc: { GEODE_UNWRAP_INTO(auto v, this->be(1)); return Ok(matjson::Value(static_cast<std::intmax_t>(v))); }
            case 0xcd: { GEODE_UNWRAP_INTO(auto v, this->be(2)); return Ok(matjson::Value(static_cast<std::intmax_t>(v))); }
            case 0xce: { GEODE_UNWRAP_INTO(auto v, this->be(4)); return Ok(matjson::Value(static_cast<std::intmax_t>(v))); }
            case 0xcf: { GEODE_UNWRAP_INTO(auto v, this->be(8)); return Ok(matjson::Value(static_cast<std::intmax_t>(v))); }
            case 0xd0: { GEODE_UNWRAP_INTO(auto v, this->be(1)); return Ok(matjson::Value(static_cast<std::intmax_t>(static_cast<int8_t>(v)))); }
            case 0xd1: { GEODE_UNWRAP_INTO(auto v, this->be(2)); return Ok(matjson::Value(static_cast<std::intmax_t>(static_cast<int16_t>(v)))); }
            case 0xd2: { GEODE_UNWRAP_INTO(auto v, this->be(4)); return Ok(matjson::Value(static_cast<std::intmax_t>(static_cast<int32_t>(v)))); }
            case 0xd3: { GEODE_UNWRAP_INTO(auto v, this->be(8)); return Ok(matjson::Value(static_cast<std::intmax_t>(static_cast<int64_t>(v)))); }
            case 0xd4: GEODE_UNWRAP(this->skip(2)); return Ok(matjson::Value(nullptr));
            case 0xd5: GEODE_UNWRAP(this->skip(3)); return Ok(matjson::Value(nullptr));
            case 0xd6: GEODE_UNWRAP(this->skip(5)); return Ok(matjson::Value(nullptr));
            case 0xd7: GEODE_UNWRAP(this->skip(9)); return Ok(matjson::Value(nullptr));
            case 0xd8: GEODE_UNWRAP(this->skip(17)); return Ok(matjson::Value(nullptr));
            case 0xd9: { GEODE_UNWRAP_INTO(auto n, this->be(1)); return this->str(n); }
            case 0xda: { GEODE_UNWRAP_INTO(auto n, this->be(2)); return this->str(n); }
            case 0xdb: { GEODE_UNWRAP_INTO(auto n, this->be(4)); return this->str(n); }
            case 0xdc: { GEODE_UNWRAP_INTO(auto n, this->be(2)); return this->array(n, depth); }
            case 0xdd: { GEODE_UNWRAP_INTO(auto n, this->be(4)); return this->array(n, depth); }
            case 0xde: { GEODE_UNWRAP_INTO(auto n, this->be(2)); return this->map(n, depth); }
            case 0xdf: { GEODE_UNWRAP_INTO(auto n, this->be(4)); return this->map(n, depth); }
            default: return Err(fmt::format("msgpack: unknown type byte 0x{:02x}", b));
        }
    }

private:
    std::span<uint8_t const> m_data;
    size_t m_pos = 0;

    Result<uint8_t> u8() {
        if (m_pos >= m_data.size()) return Err("msgpack: unexpected end of file");
        return Ok(m_data[m_pos++]);
    }
    Result<uint64_t> be(size_t bytes) {
        if (m_pos + bytes > m_data.size()) return Err("msgpack: unexpected end of file");
        uint64_t v = 0;
        for (size_t i = 0; i < bytes; i++) v = (v << 8) | m_data[m_pos++];
        return Ok(v);
    }
    Result<> skip(uint64_t n) {
        if (m_pos + n > m_data.size()) return Err("msgpack: unexpected end of file");
        m_pos += n;
        return Ok();
    }
    Result<matjson::Value> str(uint64_t n) {
        if (m_pos + n > m_data.size()) return Err("msgpack: unexpected end of file");
        std::string s(reinterpret_cast<char const*>(m_data.data() + m_pos), n);
        m_pos += n;
        return Ok(matjson::Value(s));
    }
    Result<matjson::Value> array(uint64_t n, int depth) {
        if (n > m_data.size()) return Err("msgpack: bad array size");
        auto arr = matjson::Value::array();
        for (uint64_t i = 0; i < n; i++) {
            GEODE_UNWRAP_INTO(auto v, this->read(depth + 1));
            arr.push(std::move(v));
        }
        return Ok(std::move(arr));
    }
    Result<matjson::Value> map(uint64_t n, int depth) {
        if (n > m_data.size()) return Err("msgpack: bad map size");
        auto obj = matjson::Value::object();
        for (uint64_t i = 0; i < n; i++) {
            GEODE_UNWRAP_INTO(auto key, this->read(depth + 1));
            GEODE_UNWRAP_INTO(auto val, this->read(depth + 1));
            std::string k;
            if (key.isString()) k = key.asString().unwrapOr("");
            else if (key.isNumber()) k = std::to_string(key.asInt().unwrapOr(0));
            else continue;
            obj.set(k, std::move(val));
        }
        return Ok(std::move(obj));
    }
};

// ---------------------------------------------------------------------------
// Lenient JSON macro reader (GDR v1 and similar bot formats)
// ---------------------------------------------------------------------------
matjson::Value const* findKey(matjson::Value const& obj, std::initializer_list<std::string_view> keys) {
    if (!obj.isObject()) return nullptr;
    for (auto key : keys) {
        if (obj.contains(key)) return &obj[key];
    }
    return nullptr;
}

double numberOr(matjson::Value const* v, double def) {
    if (!v) return def;
    if (v->isNumber()) return v->asDouble().unwrapOr(def);
    if (v->isBool()) return v->asBool().unwrapOr(false) ? 1.0 : 0.0;
    return def;
}

bool boolOr(matjson::Value const* v, bool def) {
    if (!v) return def;
    if (v->isBool()) return v->asBool().unwrapOr(def);
    if (v->isNumber()) return v->asDouble().unwrapOr(0) != 0;
    return def;
}

Result<RawMacro> fromJson(matjson::Value const& root) {
    if (!root.isObject()) return Err("Macro file is not an object");

    RawMacro m;

    m.framerate = numberOr(findKey(root, {"framerate", "fps", "tps"}), 0);
    if (m.framerate <= 0) {
        if (auto meta = findKey(root, {"meta"})) {
            m.framerate = numberOr(findKey(*meta, {"fps", "framerate", "tps"}), 0);
        }
    }
    if (m.framerate <= 0) m.framerate = 240.0;

    m.platformer = boolOr(findKey(root, {"platformer"}), false);

    if (auto bot = findKey(root, {"bot"})) {
        auto name = findKey(*bot, {"name"});
        auto ver = numberOr(findKey(*bot, {"version"}), 0);
        if (name && name->isString() && name->asString().unwrapOr("") == "ClickIndicator" && ver < 2) m.legacyRounding = true;
    }

    if (auto level = findKey(root, {"level"})) {
        m.levelID = static_cast<uint32_t>(numberOr(findKey(*level, {"id"}), 0));
        if (auto name = findKey(*level, {"name"}); name && name->isString()) {
            m.levelName = name->asString().unwrapOr("");
        }
    }

    auto inputs = findKey(root, {"inputs", "events", "replay", "actions"});
    if (!inputs || !inputs->isArray()) return Err("Macro has no input list");

    for (auto const& in : *inputs) {
        auto frame = findKey(in, {"frame", "f"});
        if (!frame || !frame->isNumber()) continue;

        RawInput r;
        r.frame = static_cast<uint64_t>(std::max(0.0, frame->asDouble().unwrapOr(0)));
        if (auto t = findKey(in, {"t"}); t && t->isNumber()) r.time = t->asDouble().unwrapOr(-1.0);
        r.down = boolOr(findKey(in, {"down", "holding", "press", "pressed"}), false);
        r.p2 = boolOr(findKey(in, {"2p", "p2", "player2", "player_2"}), false);
        r.button = static_cast<int>(numberOr(findKey(in, {"btn", "button"}), 1));
        m.inputs.push_back(r);
    }
    return Ok(std::move(m));
}

Result<RawMacro> fromGdr2(std::span<uint8_t> data) {
    gdr::Result<gdr::Replay<>> res = gdr::Replay<>::importData(data);
    if (res.isErr()) return Err(res.unwrapErr());

    auto const& replay = res.unwrap();
    RawMacro m;
    m.framerate = replay.framerate > 0 ? replay.framerate : 240.0;
    m.platformer = replay.platformer;
    m.levelID = replay.levelInfo.id;
    m.levelName = replay.levelInfo.name;
    for (auto const& in : replay.inputs) {
        m.inputs.push_back({in.frame, -1.0, static_cast<int>(in.button), in.player2, in.down});
    }
    return Ok(std::move(m));
}

void buildPresses(RawMacro& raw, Macro& out) {
    std::stable_sort(raw.inputs.begin(), raw.inputs.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });

    bool down[2] = {false, false};
    double startTime[2] = {0, 0};

    for (auto const& in : raw.inputs) {
        // Only the jump button matters for the indicator
        if (in.button != 1) continue;

        int p = in.p2 ? 1 : 0;
        // Bots (xdBot, GDR) stamp an input with frame = int(levelTime * fps) + 1 at the moment
        // it is handled, so frame F happened on the tick whose clock read (F - 1) / fps.
        // Our own recordings also carry the exact clock reading, which replays sub-tick exact
        // when physics bypass runs the level at more than 240 steps a second.
        double t = in.time >= 0.0 ? in.time
            : raw.legacyRounding ? static_cast<double>(in.frame) / raw.framerate
            : static_cast<double>(in.frame > 0 ? in.frame - 1 : 0) / raw.framerate;

        if (in.down) {
            if (!down[p]) {
                down[p] = true;
                startTime[p] = t;
            }
        }
        else if (down[p]) {
            down[p] = false;
            Press press;
            press.start = startTime[p];
            press.end = std::max(t, startTime[p]);
            press.p2 = in.p2;
            out.presses.push_back(press);
        }
    }

    // A click that is never released: give it a short length
    for (int p = 0; p < 2; p++) {
        if (down[p]) {
            Press press;
            press.start = startTime[p];
            press.end = startTime[p] + 0.1;
            press.p2 = p == 1;
            out.presses.push_back(press);
        }
    }

    std::stable_sort(out.presses.begin(), out.presses.end(), [](auto const& a, auto const& b) {
        return a.start < b.start;
    });

    out.hasP2 = std::any_of(out.presses.begin(), out.presses.end(), [](auto const& p) { return p.p2; });
}

} // namespace

Result<Macro> loadMacro(std::filesystem::path const& path) {
    GEODE_UNWRAP_INTO(auto bytes, utils::file::readBinary(path));
    if (bytes.empty()) return Err("Macro file is empty");

    size_t firstChar = 0;
    while (firstChar < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[firstChar]))) {
        firstChar++;
    }

    RawMacro raw;
    try {
        if (bytes.size() >= 3 && bytes[0] == 'G' && bytes[1] == 'D' && bytes[2] == 'R') {
            GEODE_UNWRAP_INTO(raw, fromGdr2(std::span<uint8_t>(bytes.data(), bytes.size())));
        }
        else if (firstChar < bytes.size() && bytes[firstChar] == '{') {
            std::string_view text(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            auto parsed = matjson::parse(text);
            if (parsed.isErr()) return Err("Invalid JSON: " + parsed.unwrapErr().message);
            GEODE_UNWRAP_INTO(raw, fromJson(parsed.unwrap()));
        }
        else {
            MsgpackReader reader(std::span<uint8_t const>(bytes.data(), bytes.size()));
            GEODE_UNWRAP_INTO(auto value, reader.read());
            GEODE_UNWRAP_INTO(raw, fromJson(value));
        }
    }
    catch (std::exception const& e) {
        return Err(std::string("Failed to read macro: ") + e.what());
    }

    Macro macro;
    macro.fileName = utils::string::pathToString(path.filename());
    macro.framerate = raw.framerate;
    macro.platformer = raw.platformer;
    macro.levelID = raw.levelID;
    macro.levelName = raw.levelName;
    buildPresses(raw, macro);

    if (macro.presses.empty()) return Err("Macro has no clicks");
    return Ok(std::move(macro));
}
