#include "settings.h"
#include "appearance.h"
#include "file_io.h"
#include "native_resources.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <array>
#include <filesystem>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>

namespace imago {
namespace {
HWND activeSettings{};
bool Valid(Hotkey hotkey) {
    const bool standalone=hotkey.modifiers==0 && (hotkey.key==VK_SNAPSHOT || (hotkey.key>=VK_F1 && hotkey.key<VK_F12));
    return (standalone || (hotkey.modifiers&(MOD_CONTROL|MOD_ALT))) && !(hotkey.modifiers&~(MOD_CONTROL|MOD_ALT|MOD_SHIFT)) &&
        hotkey.key>0 && hotkey.key<255 && hotkey.key!=VK_F12 && hotkey.key!=VK_CONTROL &&
        hotkey.key!=VK_MENU && hotkey.key!=VK_SHIFT && hotkey.key!=VK_LWIN && hotkey.key!=VK_RWIN;
}
constexpr wchar_t RunKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[]=L"Imago";
bool StartupEnabled() {
    DWORD bytes{};
    return RegGetValueW(HKEY_CURRENT_USER,RunKey,RunValue,RRF_RT_REG_SZ,nullptr,nullptr,&bytes)==ERROR_SUCCESS && bytes>sizeof(wchar_t);
}
// Preserve the previous registration if either settings persistence or hotkey reservation fails.
class StartupUpdate {
public:
    ~StartupUpdate(){Rollback();if(key_)RegCloseKey(key_);}
    bool Apply(bool enabled,std::wstring& error) {
        std::wstring command;
        if(enabled) {
            std::wstring executable(32768,L'\0');
            const DWORD length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
            if(!length || length>=executable.size()){error=L"无法获取程序路径，开机自启未修改。";return false;}
            executable.resize(length);command=L"\""+executable+L"\" --background";
            if(command.size()>=260){error=L"程序路径过长，无法注册开机自启，请移到更短的路径。";return false;}
        }
        LSTATUS result=enabled?RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key_,nullptr):
            RegOpenKeyExW(HKEY_CURRENT_USER,RunKey,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key_);
        if(!enabled && result==ERROR_FILE_NOT_FOUND)return true;
        if(result!=ERROR_SUCCESS){error=L"无法访问当前用户的启动设置，请检查系统权限。";return false;}
        DWORD bytes{};result=RegQueryValueExW(key_,RunValue,nullptr,&type_,nullptr,&bytes);
        existed_=result==ERROR_SUCCESS;
        if(result!=ERROR_SUCCESS && result!=ERROR_FILE_NOT_FOUND){error=L"无法读取原有启动设置，未进行修改。";return false;}
        if(existed_) {
            if(bytes>65536){error=L"原有启动项数据异常，未进行修改。";return false;}
            previous_.resize(bytes);
            if(RegQueryValueExW(key_,RunValue,nullptr,&type_,previous_.data(),&bytes)!=ERROR_SUCCESS){error=L"启动设置已变化，请重试。";return false;}
            previous_.resize(bytes);
        }
        if(!enabled && !existed_)return true;
        result=enabled?RegSetValueExW(key_,RunValue,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))):
            RegDeleteValueW(key_,RunValue);
        if(result!=ERROR_SUCCESS){error=L"无法更新开机自启，设置未保存。";return false;}
        changed_=true;return true;
    }
    bool Rollback() noexcept {
        if(!changed_)return true;
        const LSTATUS result=existed_?RegSetValueExW(key_,RunValue,0,type_,previous_.data(),static_cast<DWORD>(previous_.size())):
            RegDeleteValueW(key_,RunValue);
        if(result==ERROR_SUCCESS || (!existed_ && result==ERROR_FILE_NOT_FOUND)){changed_=false;return true;}
        return false;
    }
    void Commit(){changed_=false;}
private:
    HKEY key_{};DWORD type_{};bool existed_{},changed_{};std::vector<BYTE> previous_;
};
std::wstring SettingsPath() {
    PWSTR raw{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)))return {};
    struct FreePath{void operator()(wchar_t* path)const{CoTaskMemFree(path);}};
    std::unique_ptr<wchar_t,FreePath> path(raw);
    return (std::filesystem::path(path.get())/L"Imago"/L"settings.ini").wstring();
}
bool SaveSettings(const Settings& settings) {
    const auto path=SettingsPath();if(path.empty())return false;
    std::error_code code;std::filesystem::create_directories(std::filesystem::path(path).parent_path(),code);
    if(code)return false;
    const auto data=std::string("[Imago]\r\nCaptureModifiers=")+std::to_string(settings.capture.modifiers)+
        "\r\nCaptureKey="+std::to_string(settings.capture.key)+"\r\nPinModifiers="+std::to_string(settings.pin.modifiers)+
        "\r\nPinKey="+std::to_string(settings.pin.key)+"\r\nWindowSnap="+(settings.windowSnap?"1":"0")+
        "\r\nSystemAccent="+(settings.systemAccent?"1":"0")+"\r\nAccentColor="+std::to_string(settings.accentColor&0xffffff)+
        "\r\nAnimations="+(settings.animations?"1":"0")+"\r\nCheckUpdates="+(settings.checkUpdates?"1":"0")+"\r\n";
    return WriteFileAtomically(path,std::as_bytes(std::span(data.data(),data.size())))==FileWriteResult::Saved;
}
WORD HotkeyWord(Hotkey key) {
    BYTE flags=0;if(key.modifiers&MOD_CONTROL)flags|=HOTKEYF_CONTROL;
    if(key.modifiers&MOD_ALT)flags|=HOTKEYF_ALT;if(key.modifiers&MOD_SHIFT)flags|=HOTKEYF_SHIFT;
    return MAKEWORD(key.key,flags);
}
Hotkey ReadHotkey(HWND dialog,int control) {
    const WORD value=static_cast<WORD>(SendDlgItemMessageW(dialog,control,HKM_GETHOTKEY,0,0));
    UINT modifiers=0;if(HIBYTE(value)&HOTKEYF_CONTROL)modifiers|=MOD_CONTROL;
    if(HIBYTE(value)&HOTKEYF_ALT)modifiers|=MOD_ALT;if(HIBYTE(value)&HOTKEYF_SHIFT)modifiers|=MOD_SHIFT;
    return {modifiers,LOBYTE(value)};
}
#include "settings_ui.inl"
}
Settings LoadSettings() {
    Settings settings;settings.startup=StartupEnabled();const auto path=SettingsPath();if(path.empty())return settings;
    auto number=[&](const wchar_t* name,UINT fallback){return GetPrivateProfileIntW(L"Imago",name,static_cast<INT>(fallback),path.c_str());};
    Hotkey capture{number(L"CaptureModifiers",settings.capture.modifiers),number(L"CaptureKey",settings.capture.key)};
    Hotkey pin{number(L"PinModifiers",settings.pin.modifiers),number(L"PinKey",settings.pin.key)};
    if(Valid(capture) && Valid(pin) && capture!=pin){settings.capture=capture;settings.pin=pin;}
    settings.windowSnap=number(L"WindowSnap",1)!=0;
    settings.systemAccent=number(L"SystemAccent",1)!=0;
    const auto accent=number(L"AccentColor",settings.accentColor);
    if(accent<=0xffffff)settings.accentColor=accent;
    settings.animations=number(L"Animations",1)!=0;settings.checkUpdates=number(L"CheckUpdates",1)!=0;return settings;
}
std::wstring HotkeyText(Hotkey hotkey) {
    std::wstring text;if(hotkey.modifiers&MOD_CONTROL)text+=L"Ctrl + ";
    if(hotkey.modifiers&MOD_ALT)text+=L"Alt + ";if(hotkey.modifiers&MOD_SHIFT)text+=L"Shift + ";
    if(hotkey.key==VK_SNAPSHOT)return text+L"Print Screen";
    wchar_t name[80]{};UINT scan=MapVirtualKeyW(hotkey.key,MAPVK_VK_TO_VSC);
    if(hotkey.key==VK_LEFT || hotkey.key==VK_RIGHT || hotkey.key==VK_UP || hotkey.key==VK_DOWN ||
       hotkey.key==VK_PRIOR || hotkey.key==VK_NEXT || hotkey.key==VK_END || hotkey.key==VK_HOME || hotkey.key==VK_DELETE || hotkey.key==VK_INSERT)scan|=0x100;
    if(GetKeyNameTextW(static_cast<LONG>(scan<<16),name,80))text+=name;
    else text+=std::to_wstring(hotkey.key);return text;
}
void HotkeyBindings::Initialize(HWND owner,const Settings& settings) {
    owner_=owner;current_=settings;
    if(RegisterHotKey(owner,1,settings.capture.modifiers|MOD_NOREPEAT,settings.capture.key))captureId_=1;
    if(RegisterHotKey(owner,2,settings.pin.modifiers|MOD_NOREPEAT,settings.pin.key))pinId_=2;
}
void HotkeyBindings::Clear() {
    if(captureId_)UnregisterHotKey(owner_,captureId_);if(pinId_)UnregisterHotKey(owner_,pinId_);
    captureId_=pinId_=0;
}
bool HotkeyBindings::ApplyAndSave(const Settings& settings,std::wstring& error) {
    if(!Valid(settings.capture) || !Valid(settings.pin)) {
        error=L"请使用 Ctrl/Alt 组合、F1–F11 或 Print Screen；F12 由系统保留。";return false;
    }
    if(settings.capture==settings.pin){error=L"截图和贴图不能使用同一个快捷键。";return false;}
    struct Pending {
        HWND owner;std::array<int,2> ids{};
        ~Pending(){for(int id:ids)if(id)UnregisterHotKey(owner,id);}
    } pending{owner_,{}};
    auto reserve=[&](Hotkey key,int index)->int {
        if(captureId_ && key==current_.capture)return captureId_;
        if(pinId_ && key==current_.pin)return pinId_;
        for(int id=1;id<=4;++id)if(id!=captureId_ && id!=pinId_ && id!=pending.ids[0] && id!=pending.ids[1]) {
            if(!RegisterHotKey(owner_,id,key.modifiers|MOD_NOREPEAT,key.key))return 0;
            pending.ids[index]=id;return id;
        }
        return 0;
    };
    const int capture=reserve(settings.capture,0);
    if(!capture){error=L"截图快捷键被占用或不可用，请换一个组合。";return false;}
    const int pin=reserve(settings.pin,1);
    if(!pin){error=L"贴图快捷键被占用或不可用，请换一个组合。";return false;}
    // Existing bindings remain registered until startup registration and settings persistence both succeed.
    StartupUpdate startup;
    if(!startup.Apply(settings.startup,error))return false;
    if(!SaveSettings(settings)) {
        error=startup.Rollback()?L"无法写入本地设置，原有设置与启动项已保留。":L"保存失败，启动项未能恢复，请重新检查开机自启设置。";
        return false;
    }
    startup.Commit();
    for(int id:{captureId_,pinId_})if(id && id!=capture && id!=pin)UnregisterHotKey(owner_,id);
    captureId_=capture;pinId_=pin;current_=settings;pending.ids={};return true;
}
bool ShowSettingsDialog(HINSTANCE instance,HWND owner,Settings& settings,HotkeyBindings& bindings) {
    settings.startup=StartupEnabled(); // Reflect changes made outside this process before opening the dialog.
    DialogState state{settings,bindings};
    const auto result=DialogBoxParamW(instance,MAKEINTRESOURCEW(IDD_SETTINGS),owner,SettingsProc,reinterpret_cast<LPARAM>(&state));
    if(result==-1)MessageBoxW(owner,L"无法打开设置窗口。",L"Imago",MB_OK|MB_ICONERROR);
    return result==IDOK;
}
bool HandleSettingsHotkey(LPARAM keyData) {
    if(!activeSettings || !IsWindow(activeSettings))return false;
    HWND focus=GetFocus();
    if(focus==GetDlgItem(activeSettings,IDC_CAPTURE_HOTKEY) || focus==GetDlgItem(activeSettings,IDC_PIN_HOTKEY)) {
        // Our existing global binding may consume this keystroke before the native editor sees it.
        SendMessageW(focus,HKM_SETHOTKEY,HotkeyWord({LOWORD(keyData),HIWORD(keyData)}),0);
    }
    return true;
}
}
