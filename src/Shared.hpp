#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Helpers implemented in main.cpp, used by the in-game menu.

std::filesystem::path macroFolder();
void refreshOverlaySettings();
bool reloadMacroForCurrentLevel();
std::string macroStatusText();

// Stats of the current attempt (empty string if no level / no macro)
std::string currentStatsText();

// Macro picker: list files in the folder, and force one for the current level
std::vector<std::string> listMacroFiles();
void useMacroForCurrentLevel(std::string const& fileName); // empty = back to automatic
std::string macroOverrideForCurrentLevel();

// Recording your own macro
bool isRecording();
void setRecording(bool on);
int recordedInputCount();
bool saveRecordingNow(); // writes <level name>.gdr.json into the macro folder

// Per-level settings: snapshot the menu settings for the current level
bool perLevelEnabled();
void savePerLevelSettings();
void clearPerLevelSettings();
void clearTroubleForCurrentLevel();

// Macro editing (acts on the macro loaded in the current level)
bool shiftCurrentMacro(double ms);
bool trimCurrentMacroHere();
std::string saveEditedMacro();        // returns the file name written, or ""
std::string exportCurrentMacroGdr2(); // returns the file name written, or ""
std::vector<double> attemptHistory();
