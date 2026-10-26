#include "recording_panel.h"
#include "recording_devices.h"
#include "native_resources.h"
#include "toolbar.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace imago {
namespace {
constexpr wchar_t PanelClass[]=L"Imago.RecordingPanel.v1";
constexpr int Back=1,Reselect=2,Start=3,Pause=4,Stop=5,Computer=6,Microphone=7,Resolution=8,FrameRate=9;
constexpr int Copy=10,Save=11,Preview=12,Close=13,RecordingMode=14,LastButton=14;
constexpr int SetupWidth=384,ActiveWidth=308,ResultWidth=344,BarHeight=48;
constexpr int Heights[]{0,720,1080,1440,2160},Rates[]{15,24,30,60};
constexpr int SetupOrder[]{Back,RecordingMode,Reselect,Computer,Microphone,Resolution,FrameRate,Start,Close};
int Pixel(float value,float scale){return static_cast<int>(std::lround(value*scale));}
int Pixel(int value,float scale){return Pixel(static_cast<float>(value),scale);}
bool Dropdown(int id){return id>=Computer && id<=FrameRate;}
std::wstring Dimensions(SIZE size){return std::to_wstring(size.cx)+L" × "+std::to_wstring(size.cy);}
struct MenuItem {
    std::wstring text,deviceId,deviceName;
    bool checked{},enabled{true},separator{};
    int value{},width{},height{};
};
struct PopupMenu {HMENU handle{CreatePopupMenu()};~PopupMenu(){if(handle)DestroyMenu(handle);}};
}
struct RecordingPanel::Impl {
    HWND window{},owner{},tooltip{};UINT message{};
    Palette palette;
    UniqueFont font;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    ToolbarRenderer renderer;
    RecordingOptions options;
    RecordingStatus status;
    std::wstring computerName,microphoneName,tooltipText;
    bool active{},excluded{},animations{},placed{},keyboardNavigation{},menuAbove{};
    bool finished{},saving{};
    int savePercent{};
    int openMenu{};
    std::array<int,10> menuHeights{};
    float scale{1};
    ~Impl(){if(openMenu)EndMenu();if(tooltip && IsWindow(tooltip))DestroyWindow(tooltip);if(window)DestroyWindow(window);}

    void Request(RecordingAction action){PostMessageW(owner,message,static_cast<WPARAM>(action),0);}
    std::wstring AudioLabel(bool computer) const {
        const bool enabled=computer?options.systemAudio:options.microphone;
        if(!enabled)return L"关闭";
        const auto& id=computer?options.systemAudioDevice:options.microphoneDevice;
        const auto& name=computer?computerName:microphoneName;
        if(id.empty())return name.empty()?L"系统默认":L"系统默认（"+name+L"）";
        return name;
    }
    void UpdateLabels() {
        SetDlgItemTextW(window,Computer,(L"电脑音频："+AudioLabel(true)+L" · 展开选择输出设备").c_str());
        SetDlgItemTextW(window,Microphone,(L"麦克风："+AudioLabel(false)+L" · 展开选择输入设备").c_str());
        const auto resolution=options.maxHeight?std::to_wstring(options.maxHeight)+L"p":L"原始";
        SetDlgItemTextW(window,Resolution,(L"分辨率："+resolution+L" · 输出 "+Dimensions(RecordingSize(options))).c_str());
        SetDlgItemTextW(window,FrameRate,(L"帧率："+std::to_wstring(options.fps)+L" FPS").c_str());
    }
    void Layout(bool position) {
        const auto& region=options.region;
        MONITORINFO info{sizeof(info)};
        const auto monitor=position || !placed?MonitorFromRect(&region,MONITOR_DEFAULTTONEAREST):MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(monitor,&info);
        scale=std::min(GetDpiForWindow(window)/96.f,std::max(.5f,(info.rcWork.right-info.rcWork.left-16.f)/SetupWidth));
        font.reset(CreateFontW(-Pixel(12,scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"));
        const int width=Pixel(finished?ResultWidth:active?ActiveWidth:SetupWidth,scale),height=Pixel(BarHeight,scale);
        RECT current{};GetWindowRect(window,&current);int x=current.left,y=current.top;
        if(position || !placed) {
            x=region.right-width;y=region.bottom+Pixel(8,scale);
            if(y+height>info.rcWork.bottom)y=region.top-height-Pixel(8,scale);
        }
        x=std::clamp(x,static_cast<int>(info.rcWork.left),std::max(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));
        y=std::clamp(y,static_cast<int>(info.rcWork.top),std::max(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-height));
        SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);placed=true;
        int left=8;
        for(int id:SetupOrder) {
            if(id==Reselect || id==Start)left+=8;
            const int buttonWidth=Dropdown(id)?40:32;
            MoveWindow(GetDlgItem(window,id),Pixel(left,scale),Pixel(8,scale),Pixel(buttonWidth,scale),Pixel(32,scale),FALSE);
            left+=buttonWidth+4;
        }
        MoveWindow(GetDlgItem(window,Pause),Pixel(196,scale),Pixel(8,scale),Pixel(32,scale),Pixel(32,scale),FALSE);
        MoveWindow(GetDlgItem(window,Stop),Pixel(232,scale),Pixel(8,scale),Pixel(32,scale),Pixel(32,scale),FALSE);
        if(finished) {
            int buttonX=196;for(int id:{Preview,Copy,Save,Close}){MoveWindow(GetDlgItem(window,id),Pixel(buttonX,scale),Pixel(8,scale),Pixel(32,scale),Pixel(32,scale),FALSE);buttonX+=36;}
        }else if(active)MoveWindow(GetDlgItem(window,Close),Pixel(268,scale),Pixel(8,scale),Pixel(32,scale),Pixel(32,scale),FALSE);
        for(int id=1;id<=LastButton;++id) {
            const bool visible=finished?(id==Preview || id==Copy || id==Save || id==Close):active?(id==Pause || id==Stop || id==Close):
                (id!=Pause && id!=Stop && id!=Copy && id!=Save && id!=Preview);
            ShowWindow(GetDlgItem(window,id),visible?SW_SHOWNOACTIVATE:SW_HIDE);
        }
        if(tooltip)SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,std::min(Pixel(440,scale),static_cast<int>(info.rcWork.right-info.rcWork.left)-16));
        menuHeights.fill(0);UpdateLabels();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    bool OpensAbove(int id) const {
        if(openMenu==id)return menuAbove;
        RECT button{};GetWindowRect(GetDlgItem(window,id),&button);
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromRect(&button,MONITOR_DEFAULTTONEAREST),&info);
        const int height=menuHeights[id]?menuHeights[id]:Pixel(id==FrameRate?136:176,scale);
        return info.rcWork.bottom-button.bottom<height && button.top-info.rcWork.top>info.rcWork.bottom-button.bottom;
    }
    void Paint() {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);
        {
        RoundFill(dc,rect,Pixel(8,scale),palette.background,palette.border);
        ScopedSelection pen(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,palette.border);
        auto divider=[&](int x){MoveToEx(dc,Pixel(x,scale),Pixel(14,scale),nullptr);LineTo(dc,Pixel(x,scale),Pixel(34,scale));};
        if(active) {
            divider(188);
            const bool paused=status.state==RecordingState::Paused;
            const auto color=finished?palette.accentInk:paused?palette.muted:palette.highContrast?palette.accentInk:RGB(232,55,65);
            SetDCBrushColor(dc,color);ScopedSelection brush(dc,GetStockObject(DC_BRUSH));SetDCPenColor(dc,color);
            Ellipse(dc,Pixel(12,scale),Pixel(20,scale),Pixel(20,scale),Pixel(28,scale));
            std::wstring text;
            if(saving)text=L"正在保存 · "+std::to_wstring(savePercent)+L"%";
            else if(status.state==RecordingState::Starting)text=L"正在准备…";
            else if(status.state==RecordingState::Finalizing)text=L"正在完成录像…";
            else {
                const auto seconds=status.elapsed/10000000;wchar_t time[48]{};
                swprintf_s(time,L"%02lld:%02lld:%02lld",seconds/3600,seconds/60%60,seconds%60);
                text=(finished?(status.state==RecordingState::Failed?L"未完成  ":L"已录制  "):paused?L"暂停  ":L"")+std::wstring(time);
            }
            ScopedSelection selected(dc,font.get());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,palette.text);
            RECT label{Pixel(30,scale),Pixel(8,scale),Pixel(180,scale),Pixel(40,scale)};
            DrawTextW(dc,text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        }else {divider(82);divider(302);}
        }
        EndPaint(window,&ps);
    }
    void PaintButton(const DRAWITEMSTRUCT& item) {
        const int id=static_cast<int>(item.CtlID);ToolbarIcon icon=ToolbarIcon::Crop;
        switch(id) {
        case Back:icon=ToolbarIcon::Screenshot;break;case Reselect:icon=ToolbarIcon::Crop;break;
        case RecordingMode:icon=ToolbarIcon::Record;break;
        case Copy:icon=ToolbarIcon::Copy;break;case Save:icon=ToolbarIcon::Save;break;
        case Preview:icon=ToolbarIcon::Play;break;case Close:icon=ToolbarIcon::Cancel;break;
        case Computer:icon=options.systemAudio?ToolbarIcon::Speaker:ToolbarIcon::SpeakerMuted;break;
        case Microphone:icon=options.microphone?ToolbarIcon::Microphone:ToolbarIcon::MicrophoneMuted;break;
        case Resolution:icon=ToolbarIcon::Resolution;break;case FrameRate:icon=ToolbarIcon::FrameRate;break;
        case Start:icon=ToolbarIcon::StartRecording;break;case Stop:icon=ToolbarIcon::Stop;break;
        case Pause:icon=status.state==RecordingState::Paused?ToolbarIcon::Play:ToolbarIcon::Pause;break;
        }
        const bool enabled=!(item.itemState&ODS_DISABLED),primary=id==Start || id==Stop || id==Copy;
        const bool selected=id==RecordingMode || openMenu==id || (id==Computer && options.systemAudio) || (id==Microphone && options.microphone);
        const float hot=enabled?((item.itemState&ODS_SELECTED)?1.f:ButtonHover(item.hwndItem)):0;
        IconButtonStyle style;style.background=palette.background;
        style.fill=primary?BlendColor(palette.accent,palette.accentText,hot*.1f):BlendColor(selected?palette.surface:palette.background,palette.border,hot);
        style.foreground=!enabled?palette.muted:primary?palette.accentText:selected?palette.accentInk:palette.text;
        style.border=palette.highContrast?palette.border:style.fill;style.focus=primary?palette.accentText:palette.accentInk;
        style.dropdown=Dropdown(id);style.dropdownUp=style.dropdown && (openMenu==id?!menuAbove:OpensAbove(id));
        style.focused=keyboardNavigation && ButtonFocusVisible(item);style.pressed=(item.itemState&ODS_SELECTED)!=0;
        renderer.Draw(item.hDC,item.rcItem,icon,style,factory.Get());
    }
    void PaintMenu(const DRAWITEMSTRUCT& draw) {
        const auto* item=reinterpret_cast<MenuItem*>(draw.itemData);if(!item)return;
        ScopedDcState saved(draw.hDC);SetDCBrushColor(draw.hDC,palette.background);
        FillRect(draw.hDC,&draw.rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const bool hot=item->enabled && (draw.itemState&ODS_SELECTED);
        const auto textColor=!item->enabled?palette.muted:hot && palette.highContrast?palette.accentText:palette.text;
        if(hot) {
            RECT highlight=draw.rcItem;InflateRect(&highlight,-Pixel(4,scale),-Pixel(2,scale));
            const auto fill=palette.highContrast?palette.accent:BlendColor(palette.surface,palette.accent,.12f);
            RoundFill(draw.hDC,highlight,Pixel(4,scale),fill,fill);
        }
        if(item->checked) {
            const auto dot=hot && palette.highContrast?palette.accentText:palette.accentInk;
            SetDCBrushColor(draw.hDC,dot);SetDCPenColor(draw.hDC,dot);
            ScopedSelection brush(draw.hDC,GetStockObject(DC_BRUSH)),pen(draw.hDC,GetStockObject(DC_PEN));
            const int x=draw.rcItem.left+Pixel(15,scale),y=(draw.rcItem.top+draw.rcItem.bottom)/2,r=Pixel(3,scale);
            Ellipse(draw.hDC,x-r,y-r,x+r,y+r);
        }
        ScopedSelection selected(draw.hDC,font.get());SetBkMode(draw.hDC,TRANSPARENT);SetTextColor(draw.hDC,textColor);
        RECT label=draw.rcItem;label.left+=Pixel(30,scale);label.right-=Pixel(14,scale);
        DrawTextW(draw.hDC,item->text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
    }
    std::vector<MenuItem> MenuItems(int id) {
        std::vector<MenuItem> items;
        auto add=[&](std::wstring text,bool checked,int value,bool enabled=true,std::wstring deviceId={},std::wstring name={}) {
            items.push_back({std::move(text),std::move(deviceId),std::move(name),checked,enabled,false,value});
        };
        if(id==Computer || id==Microphone) {
            const bool computer=id==Computer,enabled=computer?options.systemAudio:options.microphone;
            const auto& selectedId=computer?options.systemAudioDevice:options.microphoneDevice;
            auto& selectedName=computer?computerName:microphoneName;
            const auto devices=ListRecordingAudioDevices(computer);
            if(enabled && selectedId.empty())selectedName=devices.defaultName;
            add(L"关闭",!enabled,0);
            add(devices.defaultId.empty()?L"系统默认（暂无可用设备）":L"系统默认 · "+devices.defaultName,
                enabled && selectedId.empty(),1,!devices.defaultId.empty(),{},devices.defaultName);
            items.push_back({L"",L"",L"",false,false,true});
            bool found=false;
            for(const auto& device:devices.devices) {
                const bool selected=enabled && selectedId==device.id;found|=selected;
                if(selected)selectedName=device.name;
                add(device.name,selected,2,true,device.id,device.name);
            }
            if(enabled && !selectedId.empty() && !found)add(selectedName+L"（已断开）",true,2,false,selectedId,selectedName);
            if(!devices.error.empty())add(devices.error,false,0,false);
            else if(devices.devices.empty())add(computer?L"未发现可用输出设备":L"未发现可用麦克风",false,0,false);
        }else if(id==Resolution) {
            for(int height:Heights) {
                auto candidate=options;candidate.maxHeight=height;
                add((height?std::to_wstring(height)+L"p":L"原始")+L" · "+Dimensions(RecordingSize(candidate)),options.maxHeight==height,height);
            }
        }else if(id==FrameRate)for(int rate:Rates)add(std::to_wstring(rate)+L" FPS",options.fps==rate,rate);
        return items;
    }
    void ShowMenu(int id) {
        if(active || openMenu || !Dropdown(id))return;
        auto items=MenuItems(id);UpdateLabels();UniqueBrush background(CreateSolidBrush(palette.background));
        PopupMenu menu;if(!menu.handle)return;MENUINFO info{sizeof(info)};
        info.fMask=MIM_BACKGROUND;info.hbrBack=background.get();SetMenuInfo(menu.handle,&info);
        RECT button{};GetWindowRect(GetDlgItem(window,id),&button);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&button,MONITOR_DEFAULTTONEAREST),&monitor);
        HDC dc=GetDC(window);int width=Pixel(220,scale);
        if(dc) {
            {ScopedSelection selected(dc,font.get());for(const auto& item:items){SIZE size{};GetTextExtentPoint32W(dc,item.text.c_str(),static_cast<int>(item.text.size()),&size);width=std::max(width,static_cast<int>(size.cx)+Pixel(48,scale));}}
            ReleaseDC(window,dc);
        }
        width=std::min(width,std::min(Pixel(480,scale),std::max(80,static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-Pixel(24,scale))));
        int height=Pixel(8,scale);
        for(std::size_t index=0;index<items.size();++index) {
            auto& item=items[index];item.width=width;item.height=Pixel(32,scale);
            MENUITEMINFOW entry{sizeof(entry)};entry.fMask=MIIM_FTYPE;
            if(item.separator){entry.fType=MFT_SEPARATOR;height+=Pixel(8,scale);}
            else {
                entry.fMask|=MIIM_ID|MIIM_DATA|MIIM_STATE|MIIM_STRING;entry.fType=MFT_OWNERDRAW|MFT_RADIOCHECK;
                entry.wID=static_cast<UINT>(index+1);entry.dwItemData=reinterpret_cast<ULONG_PTR>(&item);
                entry.dwTypeData=item.text.data();entry.cch=static_cast<UINT>(item.text.size());
                entry.fState=(item.enabled?MFS_ENABLED:MFS_DISABLED)|(item.checked?MFS_CHECKED:MFS_UNCHECKED);
                height+=item.height;
            }
            if(!InsertMenuItemW(menu.handle,static_cast<UINT>(index),TRUE,&entry))return;
        }
        menuHeights[id]=height;menuAbove=OpensAbove(id);openMenu=id;
        if(tooltip)SendMessageW(tooltip,TTM_POP,0,0);
        HWND savedWindow=window;InvalidateRect(GetDlgItem(window,id),nullptr,FALSE);UpdateWindow(GetDlgItem(window,id));
        TPMPARAMS parameters{sizeof(parameters)};parameters.rcExclude=button;
        const UINT flags=TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_VERTICAL|TPM_RIGHTBUTTON|
            (menuAbove?TPM_BOTTOMALIGN:TPM_TOPALIGN)|((animations && SystemAnimationsAllowed(true))?0:TPM_NOANIMATION);
        const auto command=TrackPopupMenuEx(menu.handle,flags,button.left,menuAbove?button.top:button.bottom,savedWindow,&parameters);
        // The owner can be destroyed by display changes or application exit in the native menu loop.
        if(!IsWindow(savedWindow) || reinterpret_cast<Impl*>(GetWindowLongPtrW(savedWindow,GWLP_USERDATA))!=this)return;
        openMenu=0;PostMessageW(window,WM_NULL,0,0);
        if(command && command<=items.size()) {
            const auto& item=items[command-1];
            if(item.enabled && !item.separator) {
                if(id==Computer || id==Microphone) {
                    auto& enabled=id==Computer?options.systemAudio:options.microphone;
                    auto& endpoint=id==Computer?options.systemAudioDevice:options.microphoneDevice;
                    auto& name=id==Computer?computerName:microphoneName;
                    enabled=item.value!=0;endpoint=item.deviceId;name=item.deviceName;
                }else if(id==Resolution)options.maxHeight=item.value;
                else if(id==FrameRate)options.fps=item.value;
            }
        }
        UpdateLabels();InvalidateRect(GetDlgItem(window,id),nullptr,FALSE);SetFocus(GetDlgItem(window,id));
    }
    void Click(int id) {
        if(Dropdown(id)){ShowMenu(id);return;}
        switch(id) {
        case Back:Request(RecordingAction::Back);break;case Reselect:Request(RecordingAction::Reselect);break;
        case Start:Request(RecordingAction::Start);break;case Pause:Request(RecordingAction::Pause);break;case Stop:Request(RecordingAction::Stop);break;
        case Copy:Request(RecordingAction::Copy);break;case Save:Request(RecordingAction::Save);break;
        case Preview:Request(RecordingAction::Preview);break;case Close:Request(RecordingAction::Close);break;
        }
    }
    static LRESULT CALLBACK ButtonProc(HWND button,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(GetParent(button),GWLP_USERDATA));
        if(self && msg==WM_LBUTTONDOWN){self->keyboardNavigation=false;SendMessageW(self->window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);}
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(button,ButtonProc,3);
        return DefSubclassProc(button,msg,w,l);
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) noexcept {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,msg,w,l);
        try {
            switch(msg) {
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:self->Paint();return 0;
            case WM_DRAWITEM:{auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item.CtlType==ODT_MENU)self->PaintMenu(item);else self->PaintButton(item);return TRUE;}
            case WM_MEASUREITEM:{auto& item=*reinterpret_cast<MEASUREITEMSTRUCT*>(l);if(item.CtlType==ODT_MENU && item.itemData){const auto& data=*reinterpret_cast<MenuItem*>(item.itemData);item.itemWidth=data.width;item.itemHeight=data.height;return TRUE;}break;}
            case WM_NOTIFY:{
                auto* header=reinterpret_cast<NMHDR*>(l);
                if(header->hwndFrom==self->tooltip && header->code==TTN_GETDISPINFOW) {
                    wchar_t text[1024]{};GetWindowTextW(reinterpret_cast<HWND>(header->idFrom),text,1024);self->tooltipText=text;
                    reinterpret_cast<NMTTDISPINFOW*>(l)->lpszText=self->tooltipText.data();return 0;
                }break;
            }
            case WM_NCHITTEST:{
                POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(hwnd,&point);
                if(ChildWindowFromPointEx(hwnd,point,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED)==hwnd)return HTCAPTION;
                return HTCLIENT;
            }
            case WM_DPICHANGED:if(self->openMenu)EndMenu();self->Layout(false);return 0;
            case WM_CLOSE:if(self->openMenu)EndMenu();else self->Request(RecordingAction::Close);return 0;
            case WM_COMMAND:if(HIWORD(w)==BN_CLICKED)self->Click(LOWORD(w));return 0;
            case WM_NCDESTROY:if(self->openMenu)EndMenu();self->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
            }
        }catch(...) {
            auto* current=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
            if(current)current->Request(RecordingAction::Close);
        }
        return DefWindowProcW(hwnd,msg,w,l);
    }
};
RecordingPanel::RecordingPanel():impl_(std::make_unique<Impl>()){}
RecordingPanel::~RecordingPanel()=default;
bool RecordingPanel::Create(HINSTANCE instance,HWND owner,UINT message,ID2D1Factory* factory,const Palette& palette,bool animations) {
    auto& p=*impl_;p.owner=owner;p.message=message;p.factory=factory;p.palette=palette;p.animations=animations;
    WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=Impl::Proc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=PanelClass;
    if(!RegisterClassExW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    p.window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_CONTROLPARENT,PanelClass,L"Imago 录屏",WS_POPUP|WS_CLIPCHILDREN,
        0,0,SetupWidth,BarHeight,owner,nullptr,instance,&p);
    if(!p.window)return false;
    p.tooltip=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,p.window,nullptr,instance,nullptr);
    if(p.tooltip){SendMessageW(p.tooltip,TTM_SETMAXTIPWIDTH,0,440);SendMessageW(p.tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,400);}
    const std::pair<int,const wchar_t*> buttons[]{{Back,L"切换到截图模式"},{RecordingMode,L"录屏模式（当前）"},{Reselect,L"重新框选录屏区域"},
        {Computer,L"电脑音频：关闭"},{Microphone,L"麦克风：关闭"},{Resolution,L"分辨率：原始"},{FrameRate,L"帧率：30 FPS"},
        {Start,L"开始录制"},{Pause,L"暂停"},{Stop,L"停止录制，选择复制或保存"},
        {Preview,L"播放录像（系统默认播放器）"},{Copy,L"复制视频文件 (Ctrl+C)"},{Save,L"保存录像 (Ctrl+S)"},{Close,L"退出录屏 (Esc)"}};
    for(const auto& [id,label]:buttons) {
        HWND button=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,p.window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        if(!button)return false;InstallButtonMotion(button,animations);SetWindowSubclass(button,Impl::ButtonProc,3,0);
        if(p.tooltip) {
            TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=p.window;info.uId=reinterpret_cast<UINT_PTR>(button);
            info.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(p.tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
        }
    }
    p.excluded=SetWindowDisplayAffinity(p.window,WDA_EXCLUDEFROMCAPTURE)!=FALSE;
    ThemeWindow(p.window,p.palette,true);return true;
}
void RecordingPanel::Region(RECT region){impl_->options.region=region;impl_->Layout(true);}
void RecordingPanel::Show(bool show){if(impl_->window)ShowWindow(impl_->window,show?SW_SHOWNOACTIVATE:SW_HIDE);}
void RecordingPanel::Active(const RecordingStatus& status) {
    auto& p=*impl_;const bool first=!p.active,changed=p.status.state!=status.state;
    const bool newSecond=p.status.elapsed/10000000!=status.elapsed/10000000;
    p.active=true;p.status=status;if(first)p.Layout(false);
    if(first || changed) {
        EnableWindow(GetDlgItem(p.window,Pause),status.state==RecordingState::Recording || status.state==RecordingState::Paused);
        EnableWindow(GetDlgItem(p.window,Stop),status.state!=RecordingState::Finalizing);
        SetDlgItemTextW(p.window,Stop,status.state==RecordingState::Starting?L"取消准备":L"停止录制，选择复制或保存");
        SetDlgItemTextW(p.window,Close,L"结束并退出录屏 (Esc) · 录像保留在暂存目录");
        SetDlgItemTextW(p.window,Pause,status.state==RecordingState::Paused?L"继续录制":L"暂停录制");
    }
    if(first || changed || newSecond)InvalidateRect(p.window,nullptr,FALSE);
}
void RecordingPanel::Result(const RecordingStatus& status) {
    auto& p=*impl_;p.active=true;p.finished=true;p.saving=false;p.status=status;p.Layout(false);
    for(int id:{Copy,Save,Preview,Close})EnableWindow(GetDlgItem(p.window,id),TRUE);
    SetDlgItemTextW(p.window,Close,L"关闭工具栏 (Esc) · 录像保留在暂存目录");
    SetWindowTextW(p.window,(L"Imago · 录像完成 · "+Dimensions(RecordingSize(p.options))).c_str());
}
void RecordingPanel::Exporting(int percent) {
    auto& p=*impl_;const bool changed=!p.saving || p.savePercent!=percent;p.saving=true;p.savePercent=percent;
    for(int id:{Copy,Save,Preview})EnableWindow(GetDlgItem(p.window,id),FALSE);
    SetDlgItemTextW(p.window,Close,L"取消保存并退出 (Esc) · 原录像仍保留");
    if(changed)InvalidateRect(p.window,nullptr,FALSE);
}
void RecordingPanel::Theme(const Palette& palette,bool animations) {
    if(impl_->openMenu)EndMenu();
    impl_->palette=palette;impl_->animations=animations;ThemeWindow(impl_->window,palette,true);
    for(int id=1;id<=LastButton;++id)InstallButtonMotion(GetDlgItem(impl_->window,id),animations);
    RedrawWindow(impl_->window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
bool RecordingPanel::DialogMessage(MSG& message) {
    auto& p=*impl_;
    if(!p.window || GetAncestor(message.hwnd,GA_ROOT)!=p.window)return false;
    if(message.message==WM_KEYDOWN) {
        p.keyboardNavigation=true;SendMessageW(p.window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
        const int focused=GetDlgCtrlID(GetFocus());
        if(message.wParam==VK_ESCAPE){p.Request(RecordingAction::Close);return true;}
        if(p.finished && !p.saving && (GetKeyState(VK_CONTROL)&0x8000)) {
            if(message.wParam=='C'){p.Request(RecordingAction::Copy);return true;}
            if(message.wParam=='S'){p.Request(RecordingAction::Save);return true;}
        }
        if((message.wParam==VK_F4 || message.wParam==VK_DOWN || message.wParam==VK_UP) && Dropdown(focused)){p.ShowMenu(focused);return true;}
        if(message.wParam==VK_LEFT || message.wParam==VK_RIGHT) {
            SetFocus(GetNextDlgTabItem(p.window,GetFocus(),message.wParam==VK_LEFT));return true;
        }
        if(message.wParam==VK_RETURN && focused>=Back && focused<=LastButton) {
            if(IsWindowEnabled(GetDlgItem(p.window,focused)))SendMessageW(p.window,WM_COMMAND,MAKEWPARAM(focused,BN_CLICKED),0);return true;
        }
    }
    return IsDialogMessageW(p.window,&message)!=FALSE;
}
RecordingOptions RecordingPanel::Options() const{return impl_->options;}
HWND RecordingPanel::Window() const{return impl_->window;}
bool RecordingPanel::ExcludedFromCapture() const{return impl_->excluded;}
}
