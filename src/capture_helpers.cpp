#include "capture_helpers.h"
#include <dwmapi.h>

namespace imago {
std::vector<RECT> SnapshotWindows(RECT desktop) {
    struct State { RECT desktop; std::vector<RECT> windows; } state{desktop,{}};
    EnumWindows([](HWND window,LPARAM parameter)->BOOL {
        auto& state=*reinterpret_cast<State*>(parameter);
        if(!IsWindowVisible(window) || IsIconic(window))return TRUE;
        DWORD cloaked{};DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if(cloaked)return TRUE;
        wchar_t name[80]{};GetClassNameW(window,name,80);
        if(wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0)return TRUE;
        RECT bounds{};
        if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds))) && !GetWindowRect(window,&bounds))return TRUE;
        RECT clipped{};
        if(!IntersectRect(&clipped,&bounds,&state.desktop) || clipped.right-clipped.left<4 || clipped.bottom-clipped.top<4)return TRUE;
        OffsetRect(&clipped,-state.desktop.left,-state.desktop.top);
        try { state.windows.push_back(clipped); } catch(...) { return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&state));
    return state.windows;
}
std::optional<RECT> WindowAt(const std::vector<RECT>& windows,POINT point) {
    for(const auto& window:windows)if(PtInRect(&window,point))return window;
    return {};
}
}
