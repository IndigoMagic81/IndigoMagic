// Preset library
#include "presets.h"
#include <algorithm>

std::wstring PresetDir() {
    std::wstring d = AppDataDir() + L"\\Presets";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
std::wstring CleanPresetName(const std::wstring& s) {
    std::wstring n;
    for (wchar_t ch : s) n += (ch == L'\r' || ch == L'\n' || ch == L'\t') ? L' ' : ch;
    size_t a = n.find_first_not_of(L' '), b = n.find_last_not_of(L' ');
    n = a == std::wstring::npos ? L"" : n.substr(a, b - a + 1);
    if (n.size() > 80) n.resize(80);
    return n;
}
static std::wstring fileStem(const std::wstring& name) {
    std::wstring f;
    for (wchar_t ch : name) f += (wcschr(L"\\/:*?\"<>|", ch) || ch < 32) ? L'_' : ch;
    while (!f.empty() && (f.back() == L'.' || f.back() == L' ')) f.pop_back();
    if (f.empty()) f = L"Preset";
    // CON, PRN, AUX, NUL, COM1-9 and LPT1-9 are devices on Windows, with or without an extension
    std::wstring u = f.substr(0, f.find(L'.'));
    for (auto& ch : u) ch = towupper(ch);
    while (!u.empty() && u.back() == L' ') u.pop_back();
    bool dev = u == L"CON" || u == L"PRN" || u == L"AUX" || u == L"NUL" ||
               (u.size() == 4 && (u.compare(0, 3, L"COM") == 0 || u.compare(0, 3, L"LPT") == 0) && u[3] >= L'1' && u[3] <= L'9');
    if (dev) f = L"_" + f;
    return f;
}
std::vector<Preset> LoadPresets() {
    std::vector<Preset> v;
    std::wstring dir = PresetDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.irixbg").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            Preset p;
            p.file = dir + L"\\" + fd.cFileName;
            std::wstring t;
            if (!ReadTextFile(p.file, t)) continue;
            if (!ConfigFromText(t, p.cfg, &p.name, nullptr)) continue;
            p.name = CleanPresetName(p.name);
            if (p.name.empty()) { p.name = fd.cFileName; if (p.name.size() > 7) p.name.resize(p.name.size() - 7); }
            v.push_back(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(v.begin(), v.end(), [](const Preset& a, const Preset& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return v;
}
int FindPreset(const std::vector<Preset>& v, const std::wstring& name) {
    for (size_t i = 0; i < v.size(); i++)
        if (_wcsicmp(v[i].name.c_str(), name.c_str()) == 0) return (int)i;
    return -1;
}
bool SavePreset(const std::wstring& nameIn, const Config& c, std::wstring* err) {
    std::wstring name = CleanPresetName(nameIn);
    if (name.empty()) { if (err) *err = L"Please give the preset a name."; return false; }
    auto all = LoadPresets();
    int k = FindPreset(all, name);
    std::wstring path;
    if (k >= 0) path = all[k].file;   // overwrite in place
    else {
        std::wstring stem = fileStem(name), dir = PresetDir();
        path = dir + L"\\" + stem + L".irixbg";
        for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; n++)
            path = dir + L"\\" + stem + L" (" + std::to_wstring(n) + L").irixbg";
    }
    if (!WriteTextFile(path, ConfigToText(c, name))) { if (err) *err = L"Could not write " + path; return false; }
    return true;
}
bool DeletePreset(const std::wstring& name, std::wstring* err) {
    auto all = LoadPresets();
    int k = FindPreset(all, name);
    if (k < 0) { if (err) *err = L"That preset no longer exists."; return false; }
    if (!DeleteFileW(all[k].file.c_str())) { if (err) *err = L"Could not delete " + all[k].file; return false; }
    return true;
}
bool RenamePreset(const std::wstring& oldName, const std::wstring& newNameIn, std::wstring* err) {
    std::wstring newName = CleanPresetName(newNameIn);
    if (newName.empty()) { if (err) *err = L"Please give the preset a name."; return false; }
    auto all = LoadPresets();
    int k = FindPreset(all, oldName);
    if (k < 0) { if (err) *err = L"That preset no longer exists."; return false; }
    int other = FindPreset(all, newName);
    if (other >= 0 && other != k) { if (err) *err = L"A preset called \"" + newName + L"\" already exists."; return false; }
    Config c = all[k].cfg;
    std::wstring oldFile = all[k].file;
    // write the new file first, then remove the old one - never lose the preset
    std::wstring stem = fileStem(newName), dir = PresetDir();
    std::wstring path = dir + L"\\" + stem + L".irixbg";
    for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && _wcsicmp(path.c_str(), oldFile.c_str()) != 0; n++)
        path = dir + L"\\" + stem + L" (" + std::to_wstring(n) + L").irixbg";
    if (!WriteTextFile(path, ConfigToText(c, newName))) { if (err) *err = L"Could not write " + path; return false; }
    if (_wcsicmp(path.c_str(), oldFile.c_str()) != 0) DeleteFileW(oldFile.c_str());
    return true;
}
bool ExportPresetFile(const std::wstring& path, const std::wstring& name, const Config& c, std::wstring* err) {
    if (!WriteTextFile(path, ConfigToText(c, CleanPresetName(name)))) { if (err) *err = L"Could not write " + path; return false; }
    return true;
}
bool ImportPresetFile(const std::wstring& path, Config& c, std::wstring& name, std::wstring* err) {
    std::wstring t;
    if (!ReadTextFile(path, t)) { if (err) *err = L"Could not read " + path; return false; }
    if (!ConfigFromText(t, c, &name, err)) return false;
    name = CleanPresetName(name);
    if (name.empty()) {
        size_t s = path.find_last_of(L"\\/");
        name = path.substr(s == std::wstring::npos ? 0 : s + 1);
        size_t d = name.rfind(L'.');
        if (d != std::wstring::npos && d > 0) name = name.substr(0, d);
        name = CleanPresetName(name);
    }
    return true;
}
