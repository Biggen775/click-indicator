#pragma once

#include <Geode/Result.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// One click from the macro: the button goes down at `start` and up at `end`.
// Both are level-clock (m_levelTime) readings of the tick the input was handled on, in seconds.
struct Press {
    double start = 0.0;
    double end = 0.0;
    bool p2 = false;

    // Filled in once the level is loaded (level X positions of start / end)
    float startX = 0.f;
    float endX = 0.f;

    // Runtime state for judgments
    bool judged = false;
};

struct Macro {
    std::string fileName;
    std::string levelName;
    uint32_t levelID = 0;
    double framerate = 240.0;
    bool platformer = false;
    bool hasP2 = false;
    std::vector<Press> presses; // sorted by start time
};

// Loads .gdr (msgpack), .gdr.json / .json and .gdr2 (binary) macros.
geode::Result<Macro> loadMacro(std::filesystem::path const& path);

// The frame number bots give an input handled when the level clock read `t`
// (xdBot / GDR convention: int(levelTime * fps) + 1).
inline uint64_t frameForTime(double t, double fps) {
    if (t <= 0.0) return 1;
    return static_cast<uint64_t>(t * fps + 1e-6) + 1;
}
