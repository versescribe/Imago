#include "recording_border.h"
#include <algorithm>

namespace imago {
namespace {constexpr wchar_t BorderClass[]=L"Imago.RecordingBorder.v1";}
struct RecordingBorder::Impl {
    HWND window{};COLORREF color{};
    ~Impl(){if(window)DestroyWindow(window);}
    bool Shape() {
        RECT rect{};GetClientRect(window,&rect);
        const int stroke=std::max(2,MulDiv(2,static_cast<int>(GetDpiForWindow(window)),96));
        HRGN outer=CreateRectRgn(0,0,rect.right,rect.bottom);
        HRGN inner=CreateRectRgn(stroke,stroke,std::max(stroke,static_cast<int>(rect.right)-stroke),std::max(stroke,static_cast<int>(rect.bottom)-stroke));
        const bool combined=outer && inner && CombineRgn(outer,outer,inner,RGN_DIFF)!=ERROR;
        if(inner)DeleteObject(inner);
        if(!combined || !SetWindowRgn(window,outer,TRUE)){if(outer)DeleteObject(outer);return false;}
        return true; // SetWindowRgn takes ownership on success.
    }
    static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l) noexcept {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,w,l);
        switch(message) {
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);
            SetDCBrushColor(dc,self->color);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));EndPaint(window,&paint);return 0;
        }
        case WM_NCHITTEST:return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
        case WM_DPICHANGED:self->Shape();return 0;
        case WM_NCDESTROY:self->window=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        }
        return DefWindowProcW(window,message,w,l);
    }
};
RecordingBorder::RecordingBorder():impl_(std::make_unique<Impl>()){}
RecordingBorder::~RecordingBorder()=default;
bool RecordingBorder::Create(HINSTANCE instance,HWND owner,RECT region,const Palette& palette) {
    auto& p=*impl_;p.color=palette.accentInk;
    WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.lpfnWndProc=Impl::Proc;cls.lpszClassName=BorderClass;
    if(!RegisterClassExW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    p.window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
        BorderClass,L"Imago 实际录屏范围",WS_POPUP,region.left,region.top,region.right-region.left,region.bottom-region.top,
        owner,nullptr,instance,&p);
    return p.window && SetLayeredWindowAttributes(p.window,0,255,LWA_ALPHA) && p.Shape() && SetWindowDisplayAffinity(p.window,WDA_EXCLUDEFROMCAPTURE);
}
void RecordingBorder::Show(){ShowWindow(impl_->window,SW_SHOWNOACTIVATE);}
void RecordingBorder::Appearance(const Palette& palette,bool paused) {
    const auto color=paused?palette.muted:palette.accentInk;
    if(impl_->color!=color){impl_->color=color;InvalidateRect(impl_->window,nullptr,FALSE);}
}
}
