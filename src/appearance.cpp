#include "appearance.h"
#include "native_resources.h"
#include <dwmapi.h>
#include <roapi.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace imago {
namespace {
double Luminance(COLORREF color) {
    auto channel=[](BYTE value){const double c=value/255.;return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);};
    return .2126*channel(GetRValue(color))+.7152*channel(GetGValue(color))+.0722*channel(GetBValue(color));
}
double Contrast(COLORREF first,COLORREF second) {
    const double a=Luminance(first),b=Luminance(second);return (std::max(a,b)+.05)/(std::min(a,b)+.05);
}
COLORREF SystemAccent() {
    COLORREF result=RGB(0,103,192);
    const HRESULT initialized=RoInitialize(RO_INIT_SINGLETHREADED);
    try {
        const auto color=winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        result=RGB(color.R,color.G,color.B);
    }catch(...) {
        DWORD color{};BOOL opaque{};
        if(SUCCEEDED(DwmGetColorizationColor(&color,&opaque)))result=RGB((color>>16)&255,(color>>8)&255,color&255);
    }
    if(SUCCEEDED(initialized))RoUninitialize();
    return result;
}
constexpr UINT_PTR MotionSubclass=61,MotionTimer=61;
struct ButtonMotion {Transition hover;bool enabled{};ButtonMotion(){hover.Snap(0);}};
LRESULT CALLBACK ButtonMotionProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    auto* motion=reinterpret_cast<ButtonMotion*>(data);
    auto target=[&](float value) {
        if(motion->hover.to==value)return;
        motion->hover.Start(value,motion->enabled && SystemAnimationsAllowed());
        if(motion->hover.running && !SetTimer(window,MotionTimer,15,nullptr))motion->hover.Snap(value);
        InvalidateRect(window,nullptr,FALSE);
    };
    if(message==WM_MOUSEMOVE && IsWindowEnabled(window)) {
        target(1);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);
    }else if(message==WM_MOUSELEAVE || message==WM_ENABLE)target(0);
    else if(message==WM_TIMER && w==MotionTimer) {
        if(!SystemAnimationsAllowed())motion->hover.Snap(motion->hover.to);else motion->hover.Tick();
        if(!motion->hover.running)KillTimer(window,MotionTimer);
        InvalidateRect(window,nullptr,FALSE);return 0;
    }else if(message==WM_NCDESTROY) {
        KillTimer(window,MotionTimer);RemoveWindowSubclass(window,ButtonMotionProc,MotionSubclass);delete motion;
    }
    return DefSubclassProc(window,message,w,l);
}
}
COLORREF BlendColor(COLORREF from,COLORREF to,float amount) {
    amount=std::clamp(amount,0.f,1.f);
    auto channel=[&](BYTE a,BYTE b){return static_cast<BYTE>(std::lround(a+(b-a)*amount));};
    return RGB(channel(GetRValue(from),GetRValue(to)),channel(GetGValue(from),GetGValue(to)),channel(GetBValue(from),GetBValue(to)));
}
Palette ReadPalette(const Settings& settings) {
    HIGHCONTRASTW hc{sizeof(hc)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);
    const bool contrast=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;
    DWORD light=1,bytes=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    Palette p{};p.dark=!light && !contrast;p.highContrast=contrast;
    p.background=contrast?GetSysColor(COLOR_WINDOW):p.dark?RGB(24,24,24):RGB(246,246,246);
    p.surface=contrast?GetSysColor(COLOR_WINDOW):p.dark?RGB(34,34,34):RGB(255,255,255);
    p.text=contrast?GetSysColor(COLOR_WINDOWTEXT):p.dark?RGB(245,245,245):RGB(26,26,26);
    p.muted=contrast?GetSysColor(COLOR_WINDOWTEXT):p.dark?RGB(182,182,182):RGB(98,98,98);
    p.border=contrast?GetSysColor(COLOR_WINDOWTEXT):p.dark?RGB(58,58,58):RGB(225,225,225);
    p.accent=contrast?GetSysColor(COLOR_HIGHLIGHT):settings.systemAccent?SystemAccent():settings.accentColor&0xffffff;
    p.accentText=contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):Contrast(p.accent,RGB(255,255,255))>=Contrast(p.accent,RGB(0,0,0))?RGB(255,255,255):RGB(0,0,0);
    p.accentInk=contrast?p.text:p.accent;
    // Thin focus outlines and accent labels must also remain visible on the window surface.
    for(int i=0;!contrast && i<20 && Contrast(p.accentInk,p.surface)<3;++i)p.accentInk=BlendColor(p.accentInk,p.text,.12f);
    return p;
}
void ThemeWindow(HWND window,const Palette& p,bool rounded) {
    BOOL dark=p.dark;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corners=rounded?DWMWCP_ROUND:DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
}
bool SystemAnimationsAllowed(bool menu) {
    BOOL enabled=FALSE;
    HIGHCONTRASTW hc{sizeof(hc)};
    if(!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) || (hc.dwFlags&HCF_HIGHCONTRASTON))return false;
    if(!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) || !enabled)return false;
    if(menu && (!SystemParametersInfoW(SPI_GETMENUANIMATION,0,&enabled,0) || !enabled))return false;
    return true;
}
bool AnimationsAllowed(const Settings& settings,bool menu){return settings.animations && SystemAnimationsAllowed(menu);}
void Transition::Start(float destination,bool animate) {
    Tick();from=value;to=destination;began=GetTickCount64();running=animate && std::abs(to-from)>.001f;
    if(!running)value=to;
}
bool Transition::Tick() {
    if(!running)return false;
    const float t=std::min(1.f,static_cast<float>(GetTickCount64()-began)/140.f);
    const float remaining=1-t;
    value=from+(to-from)*(1-remaining*remaining*remaining);
    if(t>=1){value=to;running=false;}return running;
}
void InstallButtonMotion(HWND button,bool enabled) {
    DWORD_PTR data{};
    if(GetWindowSubclass(button,ButtonMotionProc,MotionSubclass,&data)) {
        auto* motion=reinterpret_cast<ButtonMotion*>(data);motion->enabled=enabled;
        if(!enabled){motion->hover.Snap(motion->hover.to);KillTimer(button,MotionTimer);}return;
    }
    auto* motion=new(std::nothrow) ButtonMotion;if(!motion)return;
    motion->enabled=enabled;
    if(!SetWindowSubclass(button,ButtonMotionProc,MotionSubclass,reinterpret_cast<DWORD_PTR>(motion)))delete motion;
}
float ButtonHover(HWND button) {
    DWORD_PTR data{};return GetWindowSubclass(button,ButtonMotionProc,MotionSubclass,&data)?reinterpret_cast<ButtonMotion*>(data)->hover.value:0.f;
}
void RoundFill(HDC dc,RECT rect,int radius,COLORREF fill,COLORREF border) {
    ScopedDcState saved(dc);
    SetDCBrushColor(dc,fill);SetDCPenColor(dc,border);
    ScopedSelection brush(dc,GetStockObject(DC_BRUSH)),pen(dc,GetStockObject(DC_PEN));
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);
}
ButtonColors ActionButtonColors(const Palette& p,bool emphasized,bool enabled,float hover) {
    if(!enabled)hover=0;
    return {
        emphasized?BlendColor(p.accent,p.accentText,hover*.1f):BlendColor(p.surface,p.border,hover),
        emphasized?p.accent:p.border,
        !enabled?p.muted:emphasized?p.accentText:p.text,
        emphasized?p.accentText:p.accentInk
    };
}
bool ButtonFocusVisible(const DRAWITEMSTRUCT& item) {
    return (item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT) &&
        !(SendMessageW(item.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);
}
void DrawFocusRing(HDC dc,RECT rect,float scale,COLORREF color,int inset) {
    const int padding=static_cast<int>(std::lround(inset*scale));
    InflateRect(&rect,-padding,-padding);
    const int radius=std::max(1,static_cast<int>(std::lround(5*scale)));
    UniquePen pen(CreatePen(PS_SOLID,std::max(1,static_cast<int>(std::lround(scale))),color));
    ScopedSelection selectedPen(dc,pen.get()),brush(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
}
}
