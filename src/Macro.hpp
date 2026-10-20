#pragma once

#include <Geode/Result.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// One click from the macro: the button goes down at `start` and up at `end`
// (both in seconds since the start of the attempt).
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
