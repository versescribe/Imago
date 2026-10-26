#pragma once
#include "settings.h"
#include <commctrl.h>

namespace imago {
struct Palette {
    bool dark{},highContrast{};
    COLORREF background{},surface{},text{},muted{},border{},accent{},accentText{},accentInk{};
};
Palette ReadPalette(const Settings& settings);
void ThemeWindow(HWND window,const Palette& palette,bool rounded);
COLORREF BlendColor(COLORREF from,COLORREF to,float amount);
bool SystemAnimationsAllowed(bool menu=false);
bool AnimationsAllowed(const Settings& settings,bool menu=false);
void InstallButtonMotion(HWND button,bool enabled);
float ButtonHover(HWND button);
void RoundFill(HDC dc,RECT rect,int radius,COLORREF fill,COLORREF border);
struct ButtonColors { COLORREF fill{},border{},text{},focus{}; };
ButtonColors ActionButtonColors(const Palette& palette,bool emphasized,bool enabled,float hover);
bool ButtonFocusVisible(const DRAWITEMSTRUCT& item);
void DrawFocusRing(HDC dc,RECT rect,float scale,COLORREF color,int inset=3);
struct Transition {
    float value{1},from{1},to{1};
    ULONGLONG began{};
    bool running{};
    void Start(float destination,bool animate);
    bool Tick();
    void Snap(float destination){value=from=to=destination;running=false;}
};
}
