#pragma once
#include <windows.h>
#include <string>

namespace imago {
struct Hotkey {
    UINT modifiers{MOD_CONTROL|MOD_ALT};
    UINT key{};
    bool operator==(const Hotkey&) const = default;
};
struct Settings {
    Hotkey capture{MOD_CONTROL|MOD_ALT,'A'},pin{MOD_CONTROL|MOD_ALT,'P'};
    bool windowSnap{true};
    bool startup{};
    bool systemAccent{true};
    COLORREF accentColor{RGB(0,103,192)};
    bool animations{true};
    bool checkUpdates{true};
};
Settings LoadSettings();
std::wstring HotkeyText(Hotkey hotkey);
class HotkeyBindings {
public:
    void Initialize(HWND owner,const Settings& settings);
    void Clear();
    bool ApplyAndSave(const Settings& settings,std::wstring& error);
    int CaptureId() const { return captureId_; }
    int PinId() const { return pinId_; }
private:
    HWND owner_{};
    Settings current_;
    int captureId_{},pinId_{};
};
bool ShowSettingsDialog(HINSTANCE instance,HWND owner,Settings& settings,HotkeyBindings& bindings);
bool HandleSettingsHotkey(LPARAM keyData);
}
