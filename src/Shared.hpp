#pragma once

#include <filesystem>
#include <string>

// Helpers implemented in main.cpp, used by the in-game menu.

// Folder where macros are looked up
std::filesystem::path macroFolder();

// Re-read settings in the running level (call after a setting changes)
void refreshOverlaySettings();

// Look for a macro for the current level again and load it. Returns true if one was loaded.
bool reloadMacroForCurrentLevel();

// One-line description of the macro loaded in the current level
std::string macroStatusText();
