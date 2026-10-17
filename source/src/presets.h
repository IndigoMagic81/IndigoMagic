// Preset library: one .irixbg text file per preset in %APPDATA%\IrixBackground\Presets
#pragma once
#include "core.h"

struct Preset {
    std::wstring name, file;
    Config cfg;
};
std::wstring PresetDir();
std::vector<Preset> LoadPresets();   // sorted by name; unreadable files are skipped
int FindPreset(const std::vector<Preset>& v, const std::wstring& name);
// Saves (or overwrites the preset with the same name). Names are trimmed; empty names are rejected.
bool SavePreset(const std::wstring& name, const Config& c, std::wstring* err);
bool DeletePreset(const std::wstring& name, std::wstring* err);
bool RenamePreset(const std::wstring& oldName, const std::wstring& newName, std::wstring* err);
bool ExportPresetFile(const std::wstring& path, const std::wstring& name, const Config& c, std::wstring* err);
bool ImportPresetFile(const std::wstring& path, Config& c, std::wstring& name, std::wstring* err);
std::wstring CleanPresetName(const std::wstring& s);
