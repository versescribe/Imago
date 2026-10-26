#include "image.h"
#include "annotations.h"
#include "png.h"
#include "toolbar.h"
#include "settings.h"
#include "appearance.h"
#include "native_resources.h"
#include "capture_helpers.h"
#include "ocr.h"
#include "scroll_controller.h"
#include "recording_panel.h"
#include "recording_border.h"
#include "recording_files.h"
#include "updates.h"
#include "version.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

using Microsoft::WRL::ComPtr;
using imago::Image;
using imago::Palette;
using imago::ThemeWindow;

namespace {
constexpr wchar_t ControllerClass[] = L"Imago.Controller.v1";
constexpr wchar_t OverlayClass[] = L"Imago.Capture.v1";
constexpr wchar_t PinClass[] = L"Imago.Pin.v1";
constexpr wchar_t WelcomeClass[] = L"Imago.Welcome.v1";
constexpr wchar_t ScrollClass[] = L"Imago.Scroll.v1", LongPreviewClass[] = L"Imago.LongPreview.v1";
constexpr UINT TrayMessage = WM_APP + 1, ShowWelcomeMessage = WM_APP + 2;
constexpr UINT CopyCommand = 101, PinCommand = 102, CancelCommand = 103;
constexpr UINT SaveCommand = 104;
constexpr UINT OcrCommand = 105, CancelOcrCommand = 106;
constexpr UINT ScrollCommand = 107, FinishScrollCommand = 108, DiscardScrollCommand = 109;
constexpr UINT ScrollUpCommand = 110, ScrollDownCommand = 111, ScrollManualCommand = 112;
constexpr UINT RecordingCommand=113,StopRecordingCommand=114,ScreenshotModeCommand=115,RecordingFolderCommand=116;
constexpr UINT RecordingPanelMessage=WM_APP+5,RecordingTimer=3;
constexpr UINT OcrCompleteMessage = WM_APP + 4;
constexpr UINT SelectToolCommand = 401, RectangleToolCommand = 402, ArrowToolCommand = 403;
constexpr UINT TextToolCommand = 404, RedactToolCommand = 405, CropToolCommand = 406;
constexpr UINT ColorCommand = 407, UndoCommand = 409, RedoCommand = 410, DeleteCommand = 411;
constexpr UINT ToolsCommand = 412;
constexpr UINT MosaicToolCommand = 413;
constexpr UINT BlurToolCommand = 414;
constexpr UINT EllipseToolCommand = 415, PickerCommand = 417;
constexpr UINT FirstSizeCommand = 450, FirstColorCommand = 460, CustomColorCommand = 468;
constexpr UINT CaptureCommand = 201, ClipboardPinCommand = 202, WelcomeCommand = 203;
constexpr UINT ClosePinsCommand = 204, ExitCommand = 205, BackgroundCommand = 206;
constexpr UINT SettingsCommand = 207;
constexpr UINT UpdateCommand=208,UpdateCompleteMessage=WM_APP+6,UpdateNoticeTimer=4,AutoUpdateTimer=5;
constexpr UINT PinResetCommand = 301, PinCloseCommand = 302, PinTopCommand = 303;
constexpr UINT CaptureTimer = 1, DeferredCaptureMessage = WM_APP + 3;
constexpr UINT ScrollTimer = 2;
constexpr int ScrollStopHotkey = 10;

float Scale(HWND window) { return static_cast<float>(GetDpiForWindow(window)) / 96.f; }
int Px(float value, float scale) { return static_cast<int>(std::lround(value * scale)); }
D2D1_COLOR_F Color(COLORREF value, float alpha = 1.f) {
    return D2D1::ColorF(GetRValue(value) / 255.f, GetGValue(value) / 255.f, GetBValue(value) / 255.f, alpha);
}
D2D1_RECT_F RectF(RECT r) {
    return D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom));
}
HFONT MakeFont(int logicalSize, float scale, int weight = FW_NORMAL) {
    return CreateFontW(-Px(static_cast<float>(logicalSize), scale), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}
RECT MonitorWork(POINT point) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcWork;
}
void ClampWindowToWork(RECT& rect, RECT work) {
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    rect.left = std::clamp(rect.left, work.left, std::max(work.left, work.right - width));
    rect.top = std::clamp(rect.top, work.top, std::max(work.top, work.bottom - height));
    rect.right = rect.left + width; rect.bottom = rect.top + height;
}

struct Canvas {
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> format;
    float fontSize{};
    bool Ensure(HWND window, const Image& image, ID2D1Factory* factory) {
        if (target) return true;
        RECT client{}; GetClientRect(window, &client);
        auto properties = D2D1::RenderTargetProperties();
        properties.dpiX = properties.dpiY = 96.f; // All capture/pin coordinates remain physical pixels.
        auto result = factory->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(client.right, client.bottom)), &target);
        if (FAILED(result)) return false;
        result = target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);
        Image thumbnail;
        const Image* display=&image;
        const auto maximum=target->GetMaximumBitmapSize();
        if(image.width>static_cast<int>(maximum) || image.height>static_cast<int>(maximum)) {
            thumbnail=imago::Thumbnail(image,static_cast<int>(std::min(maximum,4096u)));display=&thumbnail;
        }
        if (SUCCEEDED(result)) result = target->CreateBitmap(D2D1::SizeU(display->width, display->height), display->pixels.data(), display->width * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &bitmap);
        if (FAILED(result)) { Reset(); return false; }
        return true;
    }
    void Reset() { bitmap.Reset(); brush.Reset(); target.Reset(); }
    void Resize(int width, int height) {
        if (target && FAILED(target->Resize(D2D1::SizeU(std::max(width, 1), std::max(height, 1))))) Reset();
    }
    void Fill(D2D1_RECT_F rect, D2D1_COLOR_F color) {
        brush->SetColor(color); target->FillRectangle(rect, brush.Get());
    }
    void Round(D2D1_RECT_F rect, float radius, D2D1_COLOR_F fill, D2D1_COLOR_F stroke) {
        D2D1_ROUNDED_RECT rounded{rect, radius, radius};
        brush->SetColor(fill); target->FillRoundedRectangle(rounded, brush.Get());
        brush->SetColor(stroke); target->DrawRoundedRectangle(rounded, brush.Get());
    }
    void Text(const std::wstring& text, D2D1_RECT_F rect, float size, COLORREF color, IDWriteFactory* factory) {
        if (!format || fontSize != size) {
            format.Reset(); fontSize = size;
            if (FAILED(factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &format))) return;
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        brush->SetColor(Color(color));
        target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), rect, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void End() { if (FAILED(target->EndDraw())) Reset(); }
};

enum class CaptureTool { Select, Rectangle, Ellipse, Arrow, Text, Redact, Mosaic, Blur, Crop };
enum class AnnotationDrag { None, Create, Move, Resize };
struct Capture {
    Image desktop;
    RECT bounds{}, selection{}, original{}, toolbar{}, toolPalette{}, stylePalette{}, textFrame{};
    POINT anchor{}, pointer{};
    bool selected{}, dragging{},toolsExpanded{},styleExpanded{},keyboardNavigation{};
    imago::Transition toolsMotion{0,0,0},styleMotion{0,0,0};
    bool pickingColor{},pickerCopied{};
    bool ocrBusy{};
    bool recordingMode{};
    CaptureTool screenshotTool{CaptureTool::Arrow};
    std::wstring ocrMessage;
    std::vector<RECT> windows;
    std::optional<RECT> hoverWindow,pendingSnap;
    int edges{}; // 1 left, 2 top, 4 right, 8 bottom; 16 move; 0 new selection.
    float scale{1.f};
    imago::UniqueFont font;
    std::vector<HWND> buttons,toolButtons,styleButtons;
    HWND tooltip{}, hotButton{};
    imago::ToolbarRenderer toolbarRenderer;
    imago::AnnotationDocument document;
    imago::MosaicCache mosaics;
    CaptureTool tool{CaptureTool::Arrow};
    AnnotationDrag annotationDrag{AnnotationDrag::None};
    imago::Annotation originalItem;
    int annotationHandle{};
    COLORREF color{RGB(232,17,35)};
    COLORREF drawingColor{RGB(232,17,35)},redactionColor{RGB(0,0,0)};
    float stroke{3.f}, textSize{24.f};
    int mosaicSize{12};
    HWND textEdit{};
    imago::UniqueFont textFont;
    std::optional<std::size_t> textIndex;
    bool imeComposing{},resizingTextEdit{};
    Canvas canvas;
    ~Capture() { if (tooltip && IsWindow(tooltip)) DestroyWindow(tooltip); }
};
struct Pin {
    Image image;
    Canvas canvas;
    double zoom{1.0};
    BYTE opacity{255};
    bool topmost{true};
    bool ocrBusy{};
};
struct LongPreview {
    Image image;
    std::wstring note;
    int position{};
    double scale{1};
    bool ocrBusy{};
    imago::UniqueFont font;
    HWND tooltip{};
    std::vector<HWND> buttons;
    imago::ToolbarRenderer renderer;
    ~LongPreview(){if(tooltip && IsWindow(tooltip))DestroyWindow(tooltip);}
};
struct ScrollSession {
    HWND target{},progress{};
    DWORD process{},thread{};
    RECT region{},targetBounds{};
    POINT anchor{},previousCursor{};
    imago::UniqueFont font;
    bool escapeRegistered{},rawInputRegistered{},cursorOutside{},inputHeld{},resumeRequested{};
    Image thumbnail;
    imago::ScrollController capture;
};
struct App {
    HINSTANCE instance{};
    HWND controller{}, overlay{}, welcome{}, previousForeground{};
    HICON icon{};
    UINT taskbarCreated{};
    bool trayAdded{}, captureRegistered{}, pinRegistered{};
    bool modal{};
    imago::Settings settings;
    imago::HotkeyBindings hotkeys;
    imago::OcrTask ocr;
    HWND ocrSource{};
    std::unique_ptr<ScrollSession> scroll;
    std::unique_ptr<imago::RecordingPanel> recordingPanel;
    std::unique_ptr<imago::Recorder> recorder;
    std::unique_ptr<imago::RecordingBorder> recordingBorder;
    std::unique_ptr<imago::RecordingExport> recordingExport;
    std::optional<imago::RecordingStatus> recordingResult;
    bool exitRecording{},pendingRecording{};
    imago::UpdateChecker updates;
    std::optional<imago::UpdateResult> updateResult;
    bool manualUpdate{},updateNotice{};
    std::unordered_map<HWND,std::unique_ptr<LongPreview>> longPreviews;
    std::array<COLORREF,16> customColors{RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),
        RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),
        RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),
        RGB(255,255,255),RGB(255,255,255),RGB(255,255,255),RGB(255,255,255)};
    Palette palette;
    HFONT bodyFont{}, titleFont{}, smallFont{};
    std::vector<HWND> welcomeChildren;
    imago::ToolbarRenderer welcomeRenderer;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writeFactory;
    std::unique_ptr<Capture> capture;
    std::unordered_map<HWND, std::unique_ptr<Pin>> pins;
} app;

void ShowWelcome();
std::wstring UpdateButtonText();
void CheckUpdates(bool manual=true);
void CompleteUpdateCheck();
void PresentUpdateNotice();
void BeginCapture(bool recording);
void FinishCapture(bool pin);
void CloseCapture(bool restoreFocus);
void PinClipboard();
bool CreatePin(Image image, POINT location);
void LayoutToolbar();
void SaveCapture();
void RecognizeCaptureText();
void RecognizePinText(HWND window);
void CancelOcr(bool showStatus=true);
void CompleteOcr(WPARAM request);
void PaintCaptureOcrStatus(Capture& capture);
void PaintPinOcrStatus(Pin& pin,RECT client);
void StartScrollCapture();
void EnterRecordingMode();
void BackToScreenshot();
void LayoutRecordingPanel();
void HandleRecordingAction(imago::RecordingAction action);
void RecordingTick();
void ShutdownRecording();
bool ShowRecordingPanel();
void OpenRecordingFolder();
void ScrollTick();
void ScrollRawInput(HRAWINPUT input);
void EndScrollCapture(bool discard,const wchar_t* reason=L"已停止，保留已拼接部分。",bool shuttingDown=false);
void RecognizeLongText(HWND window);
LRESULT CALLBACK ScrollProc(HWND window,UINT message,WPARAM w,LPARAM l);
LRESULT CALLBACK LongPreviewProc(HWND window,UINT message,WPARAM w,LPARAM l);
bool PaintCaptureButton(const DRAWITEMSTRUCT& item);
void Notify(const wchar_t* message, bool error = false) {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = app.controller; data.uID = 1; data.uFlags = NIF_INFO;
    data.dwInfoFlags = (error ? NIIF_WARNING : NIIF_INFO) | NIIF_NOSOUND;
    wcscpy_s(data.szInfoTitle, L"Imago");
    wcsncpy_s(data.szInfo, message, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
void AddTray() {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = app.controller; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = TrayMessage; data.hIcon = app.icon;
    wcscpy_s(data.szTip, L"Imago · 截图、录屏与贴图");
    app.trayAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (app.trayAdded) { data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data); }
}
void RemoveTray() {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = app.controller; data.uID = 1;
    if (app.trayAdded) Shell_NotifyIconW(NIM_DELETE, &data);
    app.trayAdded = false;
}
void ClosePins() {
    std::vector<HWND> windows;
    for (const auto& [window, pin] : app.pins) { (void)pin; windows.push_back(window); }
    for (HWND window : windows) DestroyWindow(window);
}
void ScheduleCapture(bool recording=false) {
    if (app.modal || app.scroll) return;
    if(ShowRecordingPanel())return;
    if (app.overlay) { if(recording)EnterRecordingMode();SetForegroundWindow(app.overlay);return; }
    CancelOcr(false);
    app.previousForeground = GetForegroundWindow();
    if (app.previousForeground == app.welcome) ShowWindow(app.welcome, SW_HIDE);
    // Let tray menus and our welcome window disappear before capturing the selection background.
    if(!SetTimer(app.controller,CaptureTimer,150,nullptr)){Notify(L"无法启动框选，请重试。",true);return;}
    app.pendingRecording=recording;
}
void ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    const auto captureLabel=L"框选截图\t"+imago::HotkeyText(app.settings.capture);
    const auto pinLabel=L"贴出剪贴板图片\t"+imago::HotkeyText(app.settings.pin);
    AppendMenuW(menu, MF_STRING, CaptureCommand, captureLabel.c_str());
    AppendMenuW(menu, MF_STRING, RecordingCommand, L"开始录屏");
    AppendMenuW(menu, MF_STRING, ClipboardPinCommand, pinLabel.c_str());
    if(app.recorder)AppendMenuW(menu,MF_STRING,StopRecordingCommand,L"停止录屏，选择复制或保存");
    AppendMenuW(menu,MF_STRING,RecordingFolderCommand,L"打开录像暂存目录");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (app.pins.empty() ? MF_GRAYED : 0), ClosePinsCommand, L"关闭所有贴图");
    AppendMenuW(menu, MF_STRING, WelcomeCommand, L"打开 Imago");
    AppendMenuW(menu, MF_STRING, SettingsCommand, L"设置…");
    const auto updateText=UpdateButtonText();
    AppendMenuW(menu, MF_STRING|(app.updates.Busy()?MF_GRAYED:0), UpdateCommand, updateText.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitCommand, L"退出 Imago");
    SetMenuDefaultItem(menu, CaptureCommand, FALSE);
    SetForegroundWindow(app.controller);
    const auto command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | (imago::AnimationsAllowed(app.settings,true)?0:TPM_NOANIMATION), point.x, point.y, app.controller, nullptr);
    DestroyMenu(menu);
    PostMessageW(app.controller, WM_NULL, 0, 0);
    if (command) PostMessageW(app.controller, WM_COMMAND, command, 0);
}

bool PaintWelcomeAction(const DRAWITEMSTRUCT& item) {
    if(GetParent(item.hwndItem)!=app.welcome || (item.CtlID!=CaptureCommand && item.CtlID!=RecordingCommand))return false;
    const auto& p=app.palette;const float s=Scale(item.hwndItem);HDC dc=item.hDC;const int saved=SaveDC(dc);
    const bool capture=item.CtlID==CaptureCommand;const float hot=(item.itemState&ODS_SELECTED)?1.f:imago::ButtonHover(item.hwndItem);
    SetDCBrushColor(dc,p.background);FillRect(dc,&item.rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const auto fill=imago::BlendColor(p.surface,p.accent,p.highContrast?0:hot*.06f);
    imago::RoundFill(dc,item.rcItem,Px(9,s),fill,imago::BlendColor(p.border,p.accentInk,hot*.6f));
    RECT icon{Px(20,s),Px(22,s),Px(60,s),Px(62,s)};
    imago::IconButtonStyle style;style.background=fill;style.fill=p.accent;
    style.foreground=p.accentText;style.border=style.fill;
    app.welcomeRenderer.Draw(dc,icon,capture?imago::ToolbarIcon::Screenshot:imago::ToolbarIcon::Record,style,app.factory.Get());
    auto text=[&](const std::wstring& value,RECT rect,HFONT font,COLORREF color,UINT align=DT_LEFT) {
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&rect,align|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    };
    RECT title{Px(78,s),Px(21,s),Px(270,s),Px(49,s)};
    text(capture?L"开始截图":L"开始录屏",title,app.bodyFont,p.text);
    RECT key{Px(272,s),Px(21,s),item.rcItem.right-Px(22,s),Px(49,s)};
    if(capture)text(imago::HotkeyText(app.settings.capture),key,app.smallFont,p.muted,DT_RIGHT);
    RECT subtitle{Px(78,s),Px(52,s),item.rcItem.right-Px(20,s),Px(78,s)};
    text(capture?L"框选后标注、取色、识别文字或截长图":L"框选后录制视频，可选择电脑音频和麦克风",subtitle,app.smallFont,p.muted);
    if(imago::ButtonFocusVisible(item))imago::DrawFocusRing(dc,item.rcItem,s,p.accentInk,5);
    RestoreDC(dc,saved);return true;
}
void PaintButton(const DRAWITEMSTRUCT& item) {
    if(app.capture && GetParent(item.hwndItem)==app.overlay && PaintCaptureButton(item))return;
    if(PaintWelcomeAction(item))return;
    const auto& p = app.palette;
    const bool primary = item.CtlID == CopyCommand || item.CtlID == CaptureCommand;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool enabled = (item.itemState & ODS_DISABLED) == 0;
    imago::ScopedDcState saved(item.hDC);
    RECT rect = item.rcItem;
    SetDCBrushColor(item.hDC, p.background);
    FillRect(item.hDC, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const float hover=pressed?1.f:imago::ButtonHover(item.hwndItem);
    const auto colors=imago::ActionButtonColors(p,primary,enabled,hover);
    const int radius = Px(5, Scale(item.hwndItem));
    imago::RoundFill(item.hDC,rect,radius,colors.fill,colors.border);
    wchar_t text[100]{}; GetWindowTextW(item.hwndItem, text, 100);
    auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    auto oldFont = SelectObject(item.hDC, font);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC,colors.text);
    if (pressed) OffsetRect(&rect, 0, 1);
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if(imago::ButtonFocusVisible(item))imago::DrawFocusRing(item.hDC,item.rcItem,Scale(item.hwndItem),colors.focus);
    SelectObject(item.hDC, oldFont);
}
HWND Button(HWND parent, const wchar_t* text, UINT id, HFONT font) {
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), app.instance, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    imago::InstallButtonMotion(button,app.settings.animations);
    return button;
}

void LayoutWelcome() {
    for (HWND child : app.welcomeChildren) DestroyWindow(child);
    app.welcomeChildren.clear();
    if (app.bodyFont) DeleteObject(app.bodyFont);
    if (app.titleFont) DeleteObject(app.titleFont);
    if (app.smallFont) DeleteObject(app.smallFont);
    float s = Scale(app.welcome);
    app.bodyFont = MakeFont(15, s); app.titleFont = MakeFont(28, s, FW_SEMIBOLD); app.smallFont = MakeFont(12, s);
    auto label = [&](const wchar_t* text, int x, int y, int w, int h, HFONT font, UINT id = 0) {
        HWND child = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
            Px(static_cast<float>(x), s), Px(static_cast<float>(y), s), Px(static_cast<float>(w), s), Px(static_cast<float>(h), s),
            app.welcome, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), app.instance, nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        app.welcomeChildren.push_back(child);
    };
    label(L"Imago", 84, 26, 290, 40, app.titleFont);
    label(L"截图 · 录屏 · 贴图 · v" IMAGO_VERSION_W, 86, 70, 340, 22, app.smallFont, 501);
    const wchar_t* status = app.captureRegistered && app.pinRegistered
        ? L"已就绪 · 快捷键随时可用"
        : L"部分快捷键被占用，可在设置中修改";
    label(status, 42, 368, 480, 22, app.smallFont, 501);
    const auto captureName=L"开始截图 · "+imago::HotkeyText(app.settings.capture),pinName=L"剪贴板贴图 · "+imago::HotkeyText(app.settings.pin);
    HWND start = Button(app.welcome, captureName.c_str(), CaptureCommand, app.bodyFont);
    HWND record = Button(app.welcome,L"开始录屏",RecordingCommand,app.bodyFont);
    HWND pin = Button(app.welcome,pinName.c_str(),ClipboardPinCommand,app.smallFont);
    HWND settings = Button(app.welcome, L"设置", SettingsCommand, app.bodyFont);
    HWND updates = Button(app.welcome,UpdateButtonText().c_str(),UpdateCommand,app.smallFont);
    HWND dismiss = Button(app.welcome, L"收起到托盘", BackgroundCommand, app.smallFont);
    MoveWindow(start, Px(28, s), Px(124, s), Px(504, s), Px(104, s), TRUE);
    MoveWindow(record, Px(28, s), Px(242, s), Px(504, s), Px(104, s), TRUE);
    MoveWindow(pin, Px(28, s), Px(402, s), Px(304, s), Px(34, s), TRUE);
    MoveWindow(settings, Px(444, s), Px(38, s), Px(88, s), Px(34, s), TRUE);
    MoveWindow(updates,Px(340,s),Px(38,s),Px(96,s),Px(34,s),TRUE);
    EnableWindow(updates,!app.updates.Busy());
    MoveWindow(dismiss, Px(400, s), Px(402, s), Px(132, s), Px(34, s), TRUE);
    app.welcomeChildren.push_back(start);app.welcomeChildren.push_back(record);app.welcomeChildren.push_back(pin);app.welcomeChildren.push_back(settings);app.welcomeChildren.push_back(dismiss);
    app.welcomeChildren.push_back(updates);
    InvalidateRect(app.welcome, nullptr, TRUE);
}
void PaintWelcome(HWND window) {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
    RECT client{}; GetClientRect(window, &client);
    HBRUSH bg = CreateSolidBrush(app.palette.background); FillRect(dc, &client, bg); DeleteObject(bg);
    const float s = Scale(window);
    DrawIconEx(dc,Px(28,s),Px(32,s),app.icon,Px(42,s),Px(42,s),0,nullptr,DI_NORMAL);
    RECT status{Px(28,s),Px(374,s),Px(34,s),Px(380,s)};
    imago::RoundFill(dc,status,Px(3,s),app.palette.accentInk,app.palette.accentInk);
    EndPaint(window, &ps);
}
LRESULT CALLBACK WelcomeProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_PAINT: PaintWelcome(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: PaintButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w);
        HWND child = reinterpret_cast<HWND>(l);
        COLORREF background = app.palette.background;
        SetBkMode(dc, OPAQUE); SetBkColor(dc, background); SetDCBrushColor(dc, background);
        SetTextColor(dc, GetDlgCtrlID(child) == 501 ? app.palette.muted : app.palette.text);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_DPICHANGED: {
        RECT rect = *reinterpret_cast<RECT*>(l);
        SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutWelcome(); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == BackgroundCommand) ShowWindow(window, SW_HIDE);
        else PostMessageW(app.controller, WM_COMMAND, w, l);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
void ShowWelcome() {
    if(app.scroll)return;
    if(ShowRecordingPanel())return;
    if (!app.welcome) {
        POINT point{}; GetCursorPos(&point); RECT work = MonitorWork(point);
        app.welcome = CreateWindowExW(0, WelcomeClass, L"Imago", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            work.left + 60, work.top + 60, 520, 480, nullptr, nullptr, app.instance, nullptr);
        if (!app.welcome) return;
        const UINT dpi = GetDpiForWindow(app.welcome);
        const float s = dpi / 96.f;
        RECT rect{0, 0, Px(560, s), Px(460, s)};
        AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(app.welcome, GWL_STYLE)), FALSE, 0, dpi);
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        SetWindowPos(app.welcome, nullptr, work.left + (work.right - work.left - width) / 2,
            work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutWelcome();
    }
    ThemeWindow(app.welcome, app.palette, true);
    ShowWindow(app.welcome, SW_SHOWNORMAL); SetForegroundWindow(app.welcome);
}

#include "capture_ui.inl"
#include "ocr_ui.inl"
#include "scroll_ui.inl"
#include "recording_ui.inl"
#include "updates_ui.inl"

void ResizePin(HWND window, Pin& pin, double zoom, POINT anchor) {
    RECT old{}; GetWindowRect(window,&old);
    const double oldZoom=pin.zoom;
    RECT work=MonitorWork(anchor);
    // Bound dimensions as well as zoom so huge clipboard images remain manageable.
    const double maximum=std::min({4.0, 8192.0/pin.image.width,8192.0/pin.image.height});
    const double minimum=std::min(.1,maximum);
    pin.zoom=std::clamp(zoom,minimum,maximum);
    const int width=std::max(1,static_cast<int>(std::lround(pin.image.width*pin.zoom)));
    const int height=std::max(1,static_cast<int>(std::lround(pin.image.height*pin.zoom)));
    RECT rect{anchor.x-static_cast<LONG>(std::lround((anchor.x-old.left)*pin.zoom/oldZoom)),
              anchor.y-static_cast<LONG>(std::lround((anchor.y-old.top)*pin.zoom/oldZoom)),0,0};
    rect.right=rect.left+width; rect.bottom=rect.top+height;
    ClampWindowToWork(rect,work);
    SetWindowPos(window,nullptr,rect.left,rect.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    InvalidateRect(window,nullptr,FALSE);
}
void PinMenu(HWND window, Pin& pin, POINT point) {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,CopyCommand,L"复制图片\tCtrl+C");
    AppendMenuW(menu,MF_STRING,SaveCommand,L"保存为 PNG\tCtrl+S");
    AppendMenuW(menu,MF_STRING,pin.ocrBusy?CancelOcrCommand:OcrCommand,
        pin.ocrBusy?L"取消文字识别\tEsc":L"识别文字并复制\tCtrl+Shift+C");
    AppendMenuW(menu,MF_STRING,PinResetCommand,L"恢复原始大小\t双击");
    AppendMenuW(menu,MF_STRING|(pin.topmost?MF_CHECKED:0),PinTopCommand,L"置顶");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|MF_DISABLED,0,L"滚轮缩放 · Ctrl+滚轮调整透明度");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,PinCloseCommand,L"关闭贴图\tEsc");
    UINT command=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|(imago::AnimationsAllowed(app.settings,true)?0:TPM_NOANIMATION),point.x,point.y,window,nullptr);
    DestroyMenu(menu);
    if (command) PostMessageW(window,WM_COMMAND,command,0);
}
LRESULT CALLBACK PinProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto found=app.pins.find(window);
    if (found==app.pins.end()) return DefWindowProcW(window,message,w,l);
    auto& pin=*found->second;
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps{}; BeginPaint(window,&ps);
        if (pin.canvas.Ensure(window,pin.image,app.factory.Get())) {
            RECT rect{}; GetClientRect(window,&rect);
            pin.canvas.target->BeginDraw();
            pin.canvas.target->Clear(Color(app.palette.surface));
            pin.canvas.target->DrawBitmap(pin.canvas.bitmap.Get(),RectF(rect),1.f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            pin.canvas.brush->SetColor(Color(app.palette.border));
            auto border=RectF(rect); border.left+=.5f; border.top+=.5f; border.right-=.5f; border.bottom-=.5f;
            pin.canvas.target->DrawRectangle(border,pin.canvas.brush.Get(),1.f);
            PaintPinOcrStatus(pin,rect);
            pin.canvas.End();
        } else { PostMessageW(window,WM_CLOSE,0,0); Notify(L"无法显示贴图，请检查图片尺寸或显示驱动。",true); }
        EndPaint(window,&ps);return 0;
    }
    case WM_SIZE:pin.canvas.Resize(LOWORD(l),HIWORD(l));return 0;
    case WM_LBUTTONDOWN:
        ReleaseCapture(); SendMessageW(window,WM_NCLBUTTONDOWN,HTCAPTION,0);return 0;
    case WM_LBUTTONDBLCLK:{
        RECT rect{}; GetWindowRect(window,&rect); ResizePin(window,pin,1.0,{rect.left,rect.top});return 0;
    }
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(w)&MK_CONTROL) {
            pin.opacity=static_cast<BYTE>(std::clamp(static_cast<int>(pin.opacity)+(GET_WHEEL_DELTA_WPARAM(w)>0?16:-16),64,255));
            SetLayeredWindowAttributes(window,0,pin.opacity,LWA_ALPHA);
        } else ResizePin(window,pin,pin.zoom*std::pow(1.12,GET_WHEEL_DELTA_WPARAM(w)/120.0),{GET_X_LPARAM(l),GET_Y_LPARAM(l)});
        return 0;
    case WM_CONTEXTMENU:{
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if (point.x==-1 && point.y==-1) { RECT rect{};GetWindowRect(window,&rect);point={rect.left+20,rect.top+20}; }
        PinMenu(window,pin,point);return 0;
    }
    case WM_KEYDOWN:
        if (w==VK_ESCAPE) {if(pin.ocrBusy)CancelOcr();else DestroyWindow(window);return 0;}
        if (w=='C' && (GetKeyState(VK_CONTROL)&0x8000)) {SendMessageW(window,WM_COMMAND,(GetKeyState(VK_SHIFT)&0x8000)?OcrCommand:CopyCommand,0);return 0;}
        if (w=='S' && (GetKeyState(VK_CONTROL)&0x8000)) {SendMessageW(window,WM_COMMAND,SaveCommand,0);return 0;}
        if (w==VK_APPS || (w==VK_F10 && (GetKeyState(VK_SHIFT)&0x8000))) {SendMessageW(window,WM_CONTEXTMENU,0,-1);return 0;}
        break;
    case WM_COMMAND:
        if (LOWORD(w)==OcrCommand) {RecognizePinText(window);}
        else if (LOWORD(w)==CancelOcrCommand) {if(pin.ocrBusy)CancelOcr();}
        else if (LOWORD(w)==CopyCommand) {if(pin.ocrBusy)CancelOcr(false);std::wstring error;if(!imago::CopyImage(app.controller,pin.image,error))Notify(error.c_str(),true);}
        else if (LOWORD(w)==SaveCommand && !app.modal) {
            struct ModalFlag { ModalFlag(){app.modal=true;} ~ModalFlag(){app.modal=false;} } modal;
            // Own a snapshot while the native dialog pumps messages.
            Image image=pin.image;std::wstring error;
            auto result=imago::SavePngDialog(window,image,error);
            if(result==imago::SaveResult::Failed)Notify(error.c_str(),true);
        }
        else if (LOWORD(w)==PinCloseCommand) DestroyWindow(window);
        else if (LOWORD(w)==PinResetCommand) {RECT rect{};GetWindowRect(window,&rect);ResizePin(window,pin,1.0,{rect.left,rect.top});}
        else if (LOWORD(w)==PinTopCommand) {pin.topmost=!pin.topmost;SetWindowPos(window,pin.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
        return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DPICHANGED:InvalidateRect(window,nullptr,FALSE);return 0; // Image zoom is defined in physical pixels, not DIPs.
    case WM_DISPLAYCHANGE:{RECT rect{};GetWindowRect(window,&rect);RECT work=MonitorWork({rect.left,rect.top});ClampWindowToWork(rect,work);SetWindowPos(window,nullptr,rect.left,rect.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    case WM_NCDESTROY:
        if(app.ocrSource==window)CancelOcr(false);
        app.pins.erase(found);return DefWindowProcW(window,message,w,l);
    }
    return DefWindowProcW(window,message,w,l);
}
bool CreatePin(Image image,POINT location) {
    if (!image.valid()) return false;
    if (app.pins.size()>=24) {Notify(L"最多同时保留 24 张贴图，请先关闭部分贴图。",true);return false;}
    auto pin=std::make_unique<Pin>(); pin->image=std::move(image);
    RECT work=MonitorWork(location);
    pin->zoom=std::min({1.0,static_cast<double>(work.right-work.left-32)/pin->image.width,
                       static_cast<double>(work.bottom-work.top-32)/pin->image.height});
    const int width=std::max(1,static_cast<int>(std::lround(pin->image.width*pin->zoom)));
    const int height=std::max(1,static_cast<int>(std::lround(pin->image.height*pin->zoom)));
    RECT rect{location.x,location.y,location.x+width,location.y+height};ClampWindowToWork(rect,work);
    auto title=L"Imago — 贴图 "+std::to_wstring(pin->image.width)+L" × "+std::to_wstring(pin->image.height);
    HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,PinClass,title.c_str(),WS_POPUP,
        rect.left,rect.top,width,height,nullptr,nullptr,app.instance,nullptr);
    if(!window)return false;
    app.pins.emplace(window,std::move(pin));
    SetLayeredWindowAttributes(window,0,255,LWA_ALPHA);
    ThemeWindow(window,app.palette,false);
    ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);return true;
}
void PinClipboard() {
    if(app.modal || app.scroll)return;
    if(ShowRecordingPanel())return;
    if(app.overlay){FinishCapture(true);return;}
    std::wstring error;Image image=imago::ReadClipboardImage(app.controller,error);
    if(!image.valid()){Notify(error.c_str(),true);return;}
    POINT point{};GetCursorPos(&point);
    if(!CreatePin(std::move(image),point))Notify(L"无法创建贴图窗口。",true);
}

void RefreshAppAppearance() {
    app.palette=imago::ReadPalette(app.settings);
    if(app.recordingPanel)app.recordingPanel->Theme(app.palette,app.settings.animations);
    if(app.recordingBorder)app.recordingBorder->Appearance(app.palette,app.recorder && app.recorder->Status().state==imago::RecordingState::Paused);
    if(app.welcome){ThemeWindow(app.welcome,app.palette,true);LayoutWelcome();}
    if(app.overlay)RedrawWindow(app.overlay,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    for(const auto& [preview,state]:app.longPreviews){
        for(HWND button:state->buttons)imago::InstallButtonMotion(button,app.settings.animations);
        ThemeWindow(preview,app.palette,true);RedrawWindow(preview,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    if(app.scroll && app.scroll->progress){ThemeWindow(app.scroll->progress,app.palette,true);RedrawWindow(app.scroll->progress,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
    for(const auto& [pin,state]:app.pins){(void)state;ThemeWindow(pin,app.palette,false);InvalidateRect(pin,nullptr,FALSE);}
}
LRESULT CALLBACK ControllerProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(app.taskbarCreated && message==app.taskbarCreated){AddTray();return 0;}
    switch(message){
    case OcrCompleteMessage:CompleteOcr(w);return 0;
    case UpdateCompleteMessage:CompleteUpdateCheck();return 0;
    case RecordingPanelMessage:HandleRecordingAction(static_cast<imago::RecordingAction>(w));return 0;
    case ShowWelcomeMessage:ShowWelcome();return 0;
    case DeferredCaptureMessage:ScheduleCapture();return 0;
    case WM_HOTKEY:
        if(w==ScrollStopHotkey && app.scroll){EndScrollCapture(false);return 0;}
        if(imago::HandleSettingsHotkey(l))return 0;
        if(app.hotkeys.CaptureId() && w==static_cast<WPARAM>(app.hotkeys.CaptureId()))ScheduleCapture();
        else if(app.hotkeys.PinId() && w==static_cast<WPARAM>(app.hotkeys.PinId()))PinClipboard();return 0;
    case WM_TIMER:
        if(w==UpdateNoticeTimer){PresentUpdateNotice();return 0;}
        if(w==AutoUpdateTimer){KillTimer(window,AutoUpdateTimer);if(app.settings.checkUpdates && imago::AutomaticUpdateDue())CheckUpdates(false);return 0;}
        if(w==RecordingTimer){RecordingTick();return 0;}
        if(w==ScrollTimer){ScrollTick();return 0;}
        if(w==CaptureTimer){KillTimer(window,CaptureTimer);const bool recording=app.pendingRecording;app.pendingRecording=false;BeginCapture(recording);}return 0;
    case WM_INPUT:
        ScrollRawInput(reinterpret_cast<HRAWINPUT>(l));
        return DefWindowProcW(window,message,w,l);
    case TrayMessage:{
        UINT event=LOWORD(l);
        if(event==WM_CONTEXTMENU){POINT point{GET_X_LPARAM(w),GET_Y_LPARAM(w)};if(point.x==-1)GetCursorPos(&point);ShowTrayMenu(point);}
        else if(event==NIN_SELECT || event==NIN_KEYSELECT)ShowWelcome();
        else if(event==WM_LBUTTONDBLCLK)ScheduleCapture();
        return 0;
    }
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:case WM_DWMCOLORIZATIONCOLORCHANGED:RefreshAppAppearance();return 0;
    case WM_COMMAND:
        if(app.modal)return 0;
        switch(LOWORD(w)){
        case CaptureCommand:ScheduleCapture();break;
        case RecordingCommand:ScheduleCapture(true);break;
        case UpdateCommand:CheckUpdates();break;
        case ClipboardPinCommand:PinClipboard();break;
        case WelcomeCommand:ShowWelcome();break;
        case StopRecordingCommand:HandleRecordingAction(imago::RecordingAction::Stop);break;
        case RecordingFolderCommand:OpenRecordingFolder();break;
        case SettingsCommand:{
            if(app.scroll)break;
            if(ShowRecordingPanel())break;
            if(app.overlay){SetForegroundWindow(app.overlay);break;}
            app.modal=true;
            struct ResetModal{~ResetModal(){app.modal=false;}} reset;
            imago::ShowSettingsDialog(app.instance,app.welcome && IsWindowVisible(app.welcome)?app.welcome:window,app.settings,app.hotkeys);
            app.captureRegistered=app.hotkeys.CaptureId()!=0;app.pinRegistered=app.hotkeys.PinId()!=0;
            RefreshAppAppearance();break;
        }
        case ClosePinsCommand:ClosePins();break;
        case ExitCommand:DestroyWindow(window);break;
        }
        return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(w){app.updates.Cancel();ShutdownRecording();RemoveTray();}return 0;
    case WM_DESTROY:
        KillTimer(window,AutoUpdateTimer);KillTimer(window,UpdateNoticeTimer);app.updates.Cancel();
        ShutdownRecording();
        EndScrollCapture(true,L"",true);
        CancelOcr(false);
        while(!app.longPreviews.empty())DestroyWindow(app.longPreviews.begin()->first);
        KillTimer(window,CaptureTimer);CloseCapture(false);ClosePins();RemoveTray();
        app.hotkeys.Clear();
        if(app.welcome)DestroyWindow(app.welcome);
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
// Exceptions must never cross the OS callback boundary.
template<WNDPROC Procedure>
LRESULT CALLBACK GuardedProc(HWND window,UINT message,WPARAM w,LPARAM l) noexcept {
    try{return Procedure(window,message,w,l);}
    catch(const std::exception&){
        if(app.overlay)PostMessageW(app.overlay,WM_CLOSE,0,0);
        if(app.controller)Notify(L"操作未完成：内存或系统资源不足。请关闭部分贴图后重试。",true);
        return 0;
    }
}
bool RegisterWindowClass(const wchar_t* name,WNDPROC procedure,UINT style=0) {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style=style;cls.lpfnWndProc=procedure;cls.hInstance=app.instance;cls.hIcon=app.icon;cls.hIconSm=app.icon;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=name;
    return RegisterClassExW(&cls)!=0;
}
int Run(HINSTANCE instance,const std::wstring& commandLine) {
    app.instance=instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\Imago.NativeScreenshot.v1");
    if(!mutex)return 1;
    const bool existing=GetLastError()==ERROR_ALREADY_EXISTS;
    if(existing){
        if(commandLine==L"--background"){CloseHandle(mutex);return 0;}
        if(HWND controller=FindWindowW(ControllerClass,nullptr)){
            DWORD pid{};GetWindowThreadProcessId(controller,&pid);AllowSetForegroundWindow(pid);
            PostMessageW(controller,commandLine==L"--capture"?DeferredCaptureMessage:ShowWelcomeMessage,0,0);
        }
        CloseHandle(mutex);return 0;
    }
    app.icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE));
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&common);
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,app.factory.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(app.writeFactory.GetAddressOf())))){
        MessageBoxW(nullptr,L"无法初始化 Windows 图形组件。",L"Imago",MB_OK|MB_ICONERROR);CloseHandle(mutex);return 1;
    }
    if(!RegisterWindowClass(ControllerClass,GuardedProc<ControllerProc>) ||
       !RegisterWindowClass(WelcomeClass,GuardedProc<WelcomeProc>) ||
       !RegisterWindowClass(ScrollClass,GuardedProc<ScrollProc>) ||
       !RegisterWindowClass(LongPreviewClass,GuardedProc<LongPreviewProc>) ||
       !RegisterWindowClass(OverlayClass,GuardedProc<OverlayProc>,CS_DBLCLKS) ||
       !RegisterWindowClass(PinClass,GuardedProc<PinProc>,CS_DBLCLKS)) {CloseHandle(mutex);return 1;}
    // A hidden top-level window receives Explorer restarts and system theme/display broadcasts.
    app.controller=CreateWindowExW(WS_EX_TOOLWINDOW,ControllerClass,L"Imago Controller",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!app.controller){CloseHandle(mutex);return 1;}
    app.taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    app.settings=imago::LoadSettings();app.palette=imago::ReadPalette(app.settings);app.hotkeys.Initialize(app.controller,app.settings);
    app.captureRegistered=app.hotkeys.CaptureId()!=0;app.pinRegistered=app.hotkeys.PinId()!=0;
    if(!app.trayAdded){
        MessageBoxW(nullptr,L"无法创建系统托盘图标，Imago 将退出。",L"Imago",MB_OK|MB_ICONERROR);DestroyWindow(app.controller);CloseHandle(mutex);return 1;
    }
    if(commandLine==L"--capture")ScheduleCapture();
    else if(commandLine!=L"--background")ShowWelcome();
    if(app.settings.checkUpdates)SetTimer(app.controller,AutoUpdateTimer,10000,nullptr);
    if(!app.captureRegistered || !app.pinRegistered)Notify(L"部分快捷键已被其他程序占用。你仍可使用托盘菜单截图和贴图。",true);
    MSG message{};int result{};
    while((result=GetMessageW(&message,nullptr,0,0))>0){
        if(app.recordingPanel && app.recordingPanel->DialogMessage(message))continue;
        if(CaptureKey(message))continue;
        if(LongPreviewKey(message))continue;
        const HWND root=GetAncestor(message.hwnd,GA_ROOT);
        if(app.longPreviews.contains(root) && IsDialogMessageW(root,&message))continue;
        if(app.overlay && !app.scroll && !app.recordingPanel && IsDialogMessageW(app.overlay,&message))continue;
        if(app.welcome && root==app.welcome && IsWindowVisible(app.welcome) && IsDialogMessageW(app.welcome,&message))continue;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    if(app.icon)DestroyIcon(app.icon);
    if(app.bodyFont)DeleteObject(app.bodyFont);
    if(app.titleFont)DeleteObject(app.titleFont);
    if(app.smallFont)DeleteObject(app.smallFont);
    CloseHandle(mutex);
    return result<0?1:static_cast<int>(message.wParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;
    try{result=Run(instance,commandLine?commandLine:L"");}
    catch(const std::exception&){MessageBoxW(nullptr,L"Imago 无法继续运行：内存或系统资源不足。",L"Imago",MB_OK|MB_ICONERROR);}
    // Release COM objects before apartment teardown, including failure paths.
    app.ocr.Cancel();
    app.updates.Cancel();
    ShutdownRecording();
    if(app.controller && IsWindow(app.controller))DestroyWindow(app.controller);
    app.capture.reset();app.pins.clear();app.welcomeRenderer={};app.writeFactory.Reset();app.factory.Reset();
    if(SUCCEEDED(com))CoUninitialize();
    return result;
}
