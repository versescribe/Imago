// Capture UI implementation, included inside main.cpp's application namespace.
// The document and compositing code are independent of these HWND interactions.

UINT ToolCommand(CaptureTool tool) {
    switch(tool) {
    case CaptureTool::Select:return SelectToolCommand;
    case CaptureTool::Rectangle:return RectangleToolCommand;
    case CaptureTool::Ellipse:return EllipseToolCommand;
    case CaptureTool::Arrow:return ArrowToolCommand;
    case CaptureTool::Text:return TextToolCommand;
    case CaptureTool::Redact:return RedactToolCommand;
    case CaptureTool::Mosaic:return MosaicToolCommand;
    case CaptureTool::Blur:return BlurToolCommand;
    case CaptureTool::Crop:return CropToolCommand;
    }
    return SelectToolCommand;
}
constexpr float ToolbarWidth=520.f,ToolbarHeight=48.f,ToolPaletteWidth=336.f;
constexpr float StylePaletteWidth=304.f,StylePaletteHeight=188.f;
constexpr UINT_PTR PaletteMotionTimer=43;
constexpr COLORREF CaptureColors[]{RGB(232,17,35),RGB(247,99,12),RGB(255,185,0),RGB(16,124,16),
    RGB(0,120,212),RGB(136,23,152),RGB(0,0,0),RGB(255,255,255)};
constexpr const wchar_t* CaptureColorNames[]{L"红色",L"橙色",L"黄色",L"绿色",L"蓝色",L"紫色",L"黑色",L"白色"};
constexpr int CaptureStrokes[]{2,3,4,6,10},CaptureTextSizes[]{18,24,32,48,64},CaptureBlocks[]{8,12,20,32,48};
struct CaptureStyle {
    bool text{},redact{},mosaic{},blur{};
    float selected{};
    const int* Values() const {return mosaic?CaptureBlocks:text?CaptureTextSizes:CaptureStrokes;}
    const wchar_t* Label() const {return mosaic?(blur?L"模糊强度":L"马赛克颗粒"):text?L"字号":redact?L"实色遮盖无需线粗":L"线粗";}
};
CaptureStyle CurrentCaptureStyle() {
    const auto& c=*app.capture;
    CaptureStyle style;
    style.text=c.tool==CaptureTool::Text;style.redact=c.tool==CaptureTool::Redact;
    style.mosaic=c.tool==CaptureTool::Mosaic || c.tool==CaptureTool::Blur;style.blur=c.tool==CaptureTool::Blur;
    if(c.document.active) {
        const auto& item=c.document.items[*c.document.active];
        style.text=item.kind==imago::AnnotationKind::Text;style.redact=item.kind==imago::AnnotationKind::Redact;
        style.mosaic=item.kind==imago::AnnotationKind::Mosaic;style.blur=style.mosaic && item.mosaicStyle==imago::MosaicStyle::Blur;
        style.selected=style.mosaic?static_cast<float>(item.blockSize):style.text?item.fontSize/c.scale:item.stroke/c.scale;
    }else style.selected=style.mosaic?static_cast<float>(c.mosaicSize):style.text?c.textSize/c.scale:c.stroke/c.scale;
    return style;
}
RECT CaptureFlyoutBounds(float logicalWidth,float logicalHeight,UINT anchor) {
    const auto& c=*app.capture;
    POINT point{c.bounds.left+c.toolbar.left,c.bounds.top+c.toolbar.top};RECT work=MonitorWork(point);
    OffsetRect(&work,-c.bounds.left,-c.bounds.top);
    const int margin=Px(8,c.scale),width=Px(logicalWidth,c.scale),height=Px(logicalHeight,c.scale);
    const int anchorLeft=c.toolbar.left+Px(anchor==ColorCommand?132.f:80.f,c.scale);
    const int left=std::clamp(anchorLeft,static_cast<int>(work.left)+margin,std::max(static_cast<int>(work.left)+margin,static_cast<int>(work.right)-width-margin));
    int top=c.toolbar.top-height-margin;
    if(top<work.top+margin)top=c.toolbar.bottom+margin;
    top=std::clamp(top,static_cast<int>(work.top)+margin,std::max(static_cast<int>(work.top)+margin,static_cast<int>(work.bottom)-height-margin));
    return {left,top,left+width,top+height};
}
bool PaintStyleButton(const DRAWITEMSTRUCT& item) {
    const bool size=item.CtlID>=FirstSizeCommand && item.CtlID<FirstSizeCommand+5;
    if(!size && item.CtlID!=CustomColorCommand && item.CtlID!=DeleteCommand)return false;
    const auto& c=*app.capture;const auto& p=app.palette;const auto style=CurrentCaptureStyle();
    const bool enabled=(item.itemState&ODS_DISABLED)==0;
    const bool selected=size && !style.redact && std::abs(style.selected-style.Values()[item.CtlID-FirstSizeCommand])<.1f;
    const float hot=enabled?((item.itemState&ODS_SELECTED)?1.f:imago::ButtonHover(item.hwndItem)):0.f;
    const auto colors=imago::ActionButtonColors(p,selected,enabled,hot);
    HDC dc=item.hDC;imago::ScopedDcState saved(dc);RECT rect=item.rcItem;
    SetDCBrushColor(dc,p.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    imago::RoundFill(dc,rect,Px(5,c.scale),colors.fill,colors.border);
    SelectObject(dc,c.font.get());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,colors.text);
    if(size) {
        const int value=style.Values()[item.CtlID-FirstSizeCommand];
        if(!style.text && !style.mosaic) {
            const int thickness=std::max(1,Px(static_cast<float>(value),c.scale));
            const int center=(rect.left+rect.right)/2,y=rect.top+Px(10,c.scale);
            RECT sample{center-Px(11,c.scale),y-thickness/2,center+Px(11,c.scale),y+(thickness+1)/2};
            SetDCBrushColor(dc,colors.text);FillRect(dc,&sample,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            rect.top+=Px(15,c.scale);
        }
        const auto label=std::to_wstring(value);DrawTextW(dc,label.c_str(),-1,&rect,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
    }else {
        const auto label=item.CtlID==CustomColorCommand?L"自定义颜色…":L"删除标注";
        DrawTextW(dc,label,-1,&rect,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
    }
    if(c.keyboardNavigation && imago::ButtonFocusVisible(item))imago::DrawFocusRing(dc,item.rcItem,c.scale,colors.focus);
    return true;
}
const wchar_t* ToolLabel(CaptureTool tool) {
    switch(tool) {
    case CaptureTool::Select:return L"标注工具：选择 (V)";
    case CaptureTool::Rectangle:return L"标注工具：矩形 (R)";
    case CaptureTool::Ellipse:return L"标注工具：圆形 / 椭圆 (O)";
    case CaptureTool::Arrow:return L"标注工具：箭头 (A)";
    case CaptureTool::Text:return L"标注工具：文字 (T)";
    case CaptureTool::Redact:return L"标注工具：遮盖 (B)";
    case CaptureTool::Mosaic:return L"标注工具：像素马赛克 (M)";
    case CaptureTool::Blur:return L"标注工具：模糊马赛克 (Shift+M)";
    case CaptureTool::Crop:return L"标注工具：调整选区 (C)";
    }
    return L"标注工具";
}
bool PaintCaptureButton(const DRAWITEMSTRUCT& item) {
    auto& c=*app.capture;const auto& p=app.palette;
    if(PaintStyleButton(item))return true;
    using Icon=imago::ToolbarIcon;
    Icon icon=Icon::Select;
    switch(item.CtlID) {
    case ToolsCommand:
        switch(c.tool) {
        case CaptureTool::Select:icon=Icon::Select;break;
        case CaptureTool::Crop:icon=Icon::Crop;break;
        case CaptureTool::Rectangle:icon=Icon::Rectangle;break;
        case CaptureTool::Ellipse:icon=Icon::Ellipse;break;
        case CaptureTool::Arrow:icon=Icon::Arrow;break;
        case CaptureTool::Text:icon=Icon::Text;break;
        case CaptureTool::Redact:icon=Icon::Redact;break;
        case CaptureTool::Mosaic:icon=Icon::Mosaic;break;
        case CaptureTool::Blur:icon=Icon::Blur;break;
        }break;
    case ColorCommand:icon=Icon::Color;break;
    case PickerCommand:icon=Icon::Picker;break;
    case ArrowToolCommand:icon=Icon::Arrow;break;
    case RectangleToolCommand:icon=Icon::Rectangle;break;
    case EllipseToolCommand:icon=Icon::Ellipse;break;
    case TextToolCommand:icon=Icon::Text;break;
    case RedactToolCommand:icon=Icon::Redact;break;
    case MosaicToolCommand:icon=Icon::Mosaic;break;
    case BlurToolCommand:icon=Icon::Blur;break;
    case SelectToolCommand:icon=Icon::Select;break;
    case CropToolCommand:icon=Icon::Crop;break;
    case UndoCommand:icon=Icon::Undo;break;
    case RedoCommand:icon=Icon::Redo;break;
    case OcrCommand:icon=Icon::Ocr;break;
    case ScrollCommand:icon=Icon::Scroll;break;
    case RecordingCommand:icon=Icon::Record;break;
    case ScreenshotModeCommand:icon=Icon::Screenshot;break;
    case PinCommand:icon=Icon::Pin;break;
    case SaveCommand:icon=Icon::Save;break;
    case CopyCommand:icon=Icon::Copy;break;
    case CancelCommand:icon=Icon::Cancel;break;
    default:if(item.CtlID>=FirstColorCommand && item.CtlID<FirstColorCommand+std::size(CaptureColors))icon=Icon::Color;else return false;
    }
    const bool enabled=(item.itemState&ODS_DISABLED)==0;
    const bool primary=item.CtlID==CopyCommand;
    const bool colorSwatch=item.CtlID>=FirstColorCommand && item.CtlID<FirstColorCommand+std::size(CaptureColors);
    const bool active=item.CtlID==ScreenshotModeCommand || (colorSwatch && c.color==CaptureColors[item.CtlID-FirstColorCommand]) ||
        item.CtlID==ToolCommand(c.tool) || (item.CtlID==ColorCommand && c.styleExpanded) || (item.CtlID==ToolsCommand &&
        (c.toolsExpanded || (c.tool!=CaptureTool::Crop && c.tool!=CaptureTool::Select)));
    const float hot=enabled?((item.itemState&ODS_SELECTED)?1.f:imago::ButtonHover(item.hwndItem)):0.f;
    imago::IconButtonStyle style;
    style.background=p.background;
    style.fill=primary?imago::BlendColor(p.accent,p.accentText,hot*.1f):imago::BlendColor(active?p.surface:p.background,p.border,hot);
    style.foreground=!enabled?p.muted:primary?p.accentText:active?p.accentInk:p.text;
    style.border=colorSwatch && active?p.accentInk:p.highContrast?p.border:style.fill;
    style.focus=primary?p.accentText:p.accentInk;style.swatch=colorSwatch?CaptureColors[item.CtlID-FirstColorCommand]:c.color;
    style.dropdown=item.CtlID==ColorCommand || item.CtlID==ToolsCommand;
    if(style.dropdown) {
        const bool tools=item.CtlID==ToolsCommand;
        const auto bounds=CaptureFlyoutBounds(tools?ToolPaletteWidth:StylePaletteWidth,tools?ToolbarHeight:StylePaletteHeight,item.CtlID);
        const bool above=bounds.top<c.toolbar.top,expanded=tools?c.toolsExpanded:c.styleExpanded;
        style.dropdownUp=expanded?!above:above;
    }
    style.focused=c.keyboardNavigation && imago::ButtonFocusVisible(item);
    style.pressed=(item.itemState&ODS_SELECTED)!=0;
    return c.toolbarRenderer.Draw(item.hDC,item.rcItem,icon,style,app.factory.Get());
}
LRESULT CALLBACK ToolbarButtonProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(app.capture) {
        auto& c=*app.capture;
        if(message==WM_LBUTTONDOWN){c.keyboardNavigation=false;SendMessageW(app.overlay,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);}
        if(message==WM_MOUSEMOVE && c.hotButton!=window) {
            if(c.hotButton)InvalidateRect(c.hotButton,nullptr,FALSE);
            c.hotButton=window;InvalidateRect(window,nullptr,FALSE);
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);
        }
        if((message==WM_MOUSELEAVE || message==WM_NCDESTROY) && c.hotButton==window) {
            c.hotButton=nullptr;InvalidateRect(window,nullptr,FALSE);
        }
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ToolbarButtonProc,2);
    return DefSubclassProc(window,message,w,l);
}
void StartPaletteMotion(imago::Transition& motion,float target,bool animate) {
    motion.Start(target,animate && imago::AnimationsAllowed(app.settings,true));
    if(motion.running && !SetTimer(app.overlay,PaletteMotionTimer,15,nullptr))motion.Snap(target);
}
void CloseToolPalette(bool restoreButtonFocus=false) {
    if(!app.capture)return;
    auto& c=*app.capture;
    if(!c.toolsExpanded && !c.toolsMotion.running)return;
    c.toolsExpanded=false;
    StartPaletteMotion(c.toolsMotion,0,restoreButtonFocus);
    if(c.tooltip)SendMessageW(c.tooltip,TTM_POP,0,0);
    const bool childFocused=std::find(c.toolButtons.begin(),c.toolButtons.end(),GetFocus())!=c.toolButtons.end();
    for(HWND button:c.toolButtons){EnableWindow(button,FALSE);if(!c.toolsMotion.running)ShowWindow(button,SW_HIDE);}
    if(childFocused || restoreButtonFocus)SetFocus(GetDlgItem(app.overlay,ToolsCommand));
    SetWindowTextW(GetDlgItem(app.overlay,ToolsCommand),ToolLabel(c.tool));
    InvalidateRect(app.overlay,nullptr,FALSE);InvalidateRect(GetDlgItem(app.overlay,ToolsCommand),nullptr,FALSE);
}
void CloseStylePalette(bool restoreButtonFocus=false) {
    if(!app.capture)return;
    auto& c=*app.capture;if(!c.styleExpanded && !c.styleMotion.running)return;
    c.styleExpanded=false;
    StartPaletteMotion(c.styleMotion,0,restoreButtonFocus);
    if(c.tooltip)SendMessageW(c.tooltip,TTM_POP,0,0);
    const bool childFocused=std::find(c.styleButtons.begin(),c.styleButtons.end(),GetFocus())!=c.styleButtons.end();
    for(HWND button:c.styleButtons){EnableWindow(button,FALSE);if(!c.styleMotion.running)ShowWindow(button,SW_HIDE);}
    if(childFocused || restoreButtonFocus)SetFocus(GetDlgItem(app.overlay,ColorCommand));
    InvalidateRect(app.overlay,nullptr,FALSE);InvalidateRect(GetDlgItem(app.overlay,ColorCommand),nullptr,FALSE);
}
void LayoutToolPalette() {
    auto& c=*app.capture;
    if(!c.toolsExpanded && !c.toolsMotion.running){for(HWND button:c.toolButtons)ShowWindow(button,SW_HIDE);return;}
    c.toolPalette=CaptureFlyoutBounds(ToolPaletteWidth,ToolbarHeight,ToolsCommand);
    OffsetRect(&c.toolPalette,0,Px((c.toolPalette.top<c.toolbar.top?8.f:-8.f)*(1-c.toolsMotion.value),c.scale));
    for(std::size_t i=0;i<c.toolButtons.size();++i) {
        MoveWindow(c.toolButtons[i],c.toolPalette.left+Px(8+36.f*static_cast<float>(i),c.scale),c.toolPalette.top+Px(8,c.scale),Px(32,c.scale),Px(32,c.scale),TRUE);
        ShowWindow(c.toolButtons[i],SW_SHOWNOACTIVATE);
    }
}
void LayoutStylePalette() {
    auto& c=*app.capture;
    if(!c.styleExpanded && !c.styleMotion.running){for(HWND button:c.styleButtons)ShowWindow(button,SW_HIDE);return;}
    c.stylePalette=CaptureFlyoutBounds(StylePaletteWidth,StylePaletteHeight,ColorCommand);
    OffsetRect(&c.stylePalette,0,Px((c.stylePalette.top<c.toolbar.top?8.f:-8.f)*(1-c.styleMotion.value),c.scale));
    for(HWND button:c.styleButtons) {
        const auto id=static_cast<UINT>(GetDlgCtrlID(button));float x{},y{},width{},height{};
        if(id>=FirstSizeCommand && id<FirstSizeCommand+5){x=10+58.f*(id-FirstSizeCommand);y=32;width=52;height=32;}
        else if(id>=FirstColorCommand && id<FirstColorCommand+std::size(CaptureColors)){x=12+36.f*(id-FirstColorCommand);y=108;width=28;height=28;}
        else {x=id==CustomColorCommand?12.f:196.f;y=148;width=id==CustomColorCommand?172.f:96.f;height=28;}
        MoveWindow(button,c.stylePalette.left+Px(x,c.scale),c.stylePalette.top+Px(y,c.scale),Px(width,c.scale),Px(height,c.scale),TRUE);
        ShowWindow(button,SW_SHOWNOACTIVATE);
    }
}
void TickPaletteMotion() {
    auto& c=*app.capture;
    if(!imago::AnimationsAllowed(app.settings,true)){c.toolsMotion.Snap(c.toolsExpanded?1.f:0.f);c.styleMotion.Snap(c.styleExpanded?1.f:0.f);}
    else {c.toolsMotion.Tick();c.styleMotion.Tick();}
    LayoutToolPalette();LayoutStylePalette();
    if(!c.toolsMotion.running && !c.styleMotion.running)KillTimer(app.overlay,PaletteMotionTimer);
    InvalidateRect(app.overlay,nullptr,FALSE);
}
void RefreshToolbar() {
    if(!app.capture)return;
    auto& c=*app.capture;
    if(c.ocrBusy){CloseToolPalette();CloseStylePalette();}
    for(HWND button:c.buttons)EnableWindow(button,!c.ocrBusy || GetDlgCtrlID(button)==CancelCommand);
    EnableWindow(GetDlgItem(app.overlay,UndoCommand),!c.ocrBusy && c.document.CanUndo());
    EnableWindow(GetDlgItem(app.overlay,RedoCommand),!c.ocrBusy && c.document.CanRedo());
    SetWindowTextW(GetDlgItem(app.overlay,CancelCommand),c.ocrBusy?L"取消识别，返回编辑 (Esc)":L"取消 (Esc)");
    const auto toolLabel=std::wstring(ToolLabel(c.tool))+(c.toolsExpanded?L" · 收起工具":L" · 点击展开工具");
    SetWindowTextW(GetDlgItem(app.overlay,ToolsCommand),toolLabel.c_str());
    wchar_t colorLabel[80]{};
    swprintf_s(colorLabel,L"颜色与样式 #%02X%02X%02X · 线粗 / 字号 / 马赛克强度",GetRValue(c.color),GetGValue(c.color),GetBValue(c.color));
    SetWindowTextW(GetDlgItem(app.overlay,ColorCommand),colorLabel);
    for(HWND button:c.buttons)InvalidateRect(button,nullptr,FALSE);
    for(HWND button:c.toolButtons){EnableWindow(button,c.toolsExpanded && !c.ocrBusy);InvalidateRect(button,nullptr,FALSE);}
    const auto style=CurrentCaptureStyle();
    for(HWND button:c.styleButtons) {
        const auto id=static_cast<UINT>(GetDlgCtrlID(button));
        const bool size=id>=FirstSizeCommand && id<FirstSizeCommand+5;
        EnableWindow(button,c.styleExpanded && !c.ocrBusy && (!size || !style.redact) && (id!=DeleteCommand || c.document.active.has_value()));
        if(size) {
            const auto label=std::wstring(style.Label())+L" "+std::to_wstring(style.Values()[id-FirstSizeCommand]);
            SetWindowTextW(button,label.c_str());
        }
        InvalidateRect(button,nullptr,FALSE);
    }
    InvalidateRect(app.overlay,nullptr,FALSE);
}
void HideToolbar() {
    auto& c=*app.capture;
    if(c.recordingMode && app.recordingPanel && !app.recorder)app.recordingPanel->Show(false);
    CloseToolPalette();CloseStylePalette();
    KillTimer(app.overlay,PaletteMotionTimer);
    if(c.tooltip)SendMessageW(c.tooltip,TTM_POP,0,0);
    c.hotButton=nullptr;
    for(HWND button:c.buttons)ShowWindow(button,SW_HIDE);
    for(HWND button:c.toolButtons)ShowWindow(button,SW_HIDE);
    for(HWND button:c.styleButtons)ShowWindow(button,SW_HIDE);
}
void LayoutToolbar() {
    auto& c=*app.capture;
    if(c.recordingMode){HideToolbar();c.toolbar={};LayoutRecordingPanel();return;}
    if(!c.selected || c.dragging || c.pickingColor || c.annotationDrag!=AnnotationDrag::None){HideToolbar();return;}
    POINT screen{c.bounds.left+c.selection.right,c.bounds.top+c.selection.bottom};
    RECT work=MonitorWork(screen);OffsetRect(&work,-c.bounds.left,-c.bounds.top);
    const int margin=Px(8,c.scale),width=Px(ToolbarWidth,c.scale),height=Px(ToolbarHeight,c.scale);
    int left=std::clamp(static_cast<int>(c.selection.right)-width,static_cast<int>(work.left)+margin,
        std::max(static_cast<int>(work.left)+margin,static_cast<int>(work.right)-width-margin));
    int top=c.selection.bottom+margin;
    if(top+height>work.bottom-margin)top=c.selection.top-height-margin;
    top=std::clamp(top,static_cast<int>(work.top)+margin,
        std::max(static_cast<int>(work.top)+margin,static_cast<int>(work.bottom)-height-margin));
    c.toolbar={left,top,left+width,top+height};
    float x=8;
    for(std::size_t i=0;i<c.buttons.size();++i) {
        if(i==2 || i==6 || i==9)x+=8;
        const float buttonWidth=(i==2 || i==3)?40.f:32.f;
        MoveWindow(c.buttons[i],left+Px(x,c.scale),top+margin,Px(buttonWidth,c.scale),Px(32,c.scale),TRUE);
        ShowWindow(c.buttons[i],SW_SHOWNOACTIVATE);
        x+=buttonWidth+4;
    }
    LayoutToolPalette();LayoutStylePalette();RefreshToolbar();
}
void ToggleToolPalette() {
    auto& c=*app.capture;
    if(c.toolsExpanded){CloseToolPalette(true);return;}
    CloseStylePalette();
    c.toolsExpanded=true;StartPaletteMotion(c.toolsMotion,1,true);c.ocrMessage.clear();LayoutToolPalette();RefreshToolbar();
    HWND active=GetDlgItem(app.overlay,ToolCommand(c.tool));
    if(active)SetFocus(active);
}
void ToggleStylePalette() {
    auto& c=*app.capture;
    if(c.styleExpanded){CloseStylePalette(true);return;}
    CloseToolPalette();c.styleExpanded=true;StartPaletteMotion(c.styleMotion,1,true);c.ocrMessage.clear();LayoutStylePalette();RefreshToolbar();
    const auto style=CurrentCaptureStyle();UINT focus=FirstColorCommand;
    if(!style.redact) {
        focus=FirstSizeCommand;
        for(UINT i=0;i<5;++i)if(std::abs(style.selected-style.Values()[i])<.1f){focus+=i;break;}
    }
    SetFocus(GetDlgItem(app.overlay,focus));
}
void SyncStyleFromActive() {
    auto& c=*app.capture;
    if(!c.document.active)return;
    const auto& item=c.document.items[*c.document.active];
    c.color=item.color;c.stroke=item.stroke;c.textSize=item.fontSize;c.mosaicSize=item.blockSize;
}
float CaptureTextHeight(const imago::Annotation& item,const std::wstring& text) {
    ComPtr<IDWriteTextFormat> format;ComPtr<IDWriteTextLayout> layout;
    const auto& c=*app.capture;
    if(SUCCEEDED(app.writeFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,item.fontSize,L"zh-CN",&format)) &&
       SUCCEEDED(app.writeFactory->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format.Get(),
        std::max(1.f,item.x2-item.x1),static_cast<float>(c.desktop.height),&layout))) {
        DWRITE_TEXT_METRICS metrics{};
        if(SUCCEEDED(layout->GetMetrics(&metrics)))return std::max(item.fontSize*1.4f,metrics.height)+2*c.scale;
    }
    return item.fontSize*1.5f+2*c.scale;
}
std::wstring CaptureEditText() {
    const int length=GetWindowTextLengthW(app.capture->textEdit);
    std::wstring text(static_cast<std::size_t>(length)+1,L'\0');
    GetWindowTextW(app.capture->textEdit,text.data(),length+1);text.resize(length);return text;
}
COLORREF TextEditorBackground() {
    const auto& c=*app.capture;const auto& p=app.palette;
    if(p.highContrast)return p.surface;
    // Preserve the chosen text color while keeping white/black annotations legible during editing.
    auto luminance=[](COLORREF color) {
        auto channel=[](BYTE value){const double s=value/255.;return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
        return .2126*channel(GetRValue(color))+.7152*channel(GetGValue(color))+.0722*channel(GetBValue(color));
    };
    const double text=luminance(c.color);
    auto contrast=[&](COLORREF background){const double bg=luminance(background);return (std::max(bg,text)+.05)/(std::min(bg,text)+.05);};
    if(contrast(p.surface)>=3)return p.surface;
    const auto light=RGB(250,250,250),dark=RGB(32,32,32);
    return contrast(light)>contrast(dark)?light:dark;
}
void UpdateTextEditorSize() {
    auto& c=*app.capture;
    if(!c.textEdit || !c.textIndex || c.resizingTextEdit)return;
    c.resizingTextEdit=true;
    const auto& item=c.document.items[*c.textIndex];
    const int left=static_cast<int>(std::floor(item.x1)),top=static_cast<int>(std::floor(item.y1));
    const int width=std::max(1,static_cast<int>(std::ceil(item.x2))-left);
    const auto text=CaptureEditText();
    int height=static_cast<int>(std::ceil(CaptureTextHeight(item,text)));
    HDC dc=GetDC(c.textEdit);
    if(dc) {
        const auto previous=SelectObject(dc,c.textFont.get());TEXTMETRICW metric{};
        if(GetTextMetricsW(dc,&metric))height=std::max(height,static_cast<int>(SendMessageW(c.textEdit,EM_GETLINECOUNT,0,0))*static_cast<int>(metric.tmHeight)+Px(2,c.scale));
        SelectObject(dc,previous);ReleaseDC(c.textEdit,dc);
    }
    height=std::clamp(height,1,std::max(1,static_cast<int>(c.selection.bottom)-top));
    RECT previous{};GetWindowRect(c.textEdit,&previous);MapWindowPoints(nullptr,app.overlay,reinterpret_cast<POINT*>(&previous),2);
    if(previous.left!=left || previous.top!=top || previous.right-previous.left!=width || previous.bottom-previous.top!=height) {
        MoveWindow(c.textEdit,left,top,width,height,TRUE);
        RECT format{0,0,width,height};SendMessageW(c.textEdit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
        SendMessageW(c.textEdit,EM_SCROLLCARET,0,0);
    }
    c.textFrame={left,top,left+width,top+height};InflateRect(&c.textFrame,Px(8,c.scale),Px(8,c.scale));
    c.resizingTextEdit=false;InvalidateRect(app.overlay,nullptr,FALSE);
}
void EndTextEdit(bool accept) {
    auto& c=*app.capture;
    if(!c.textEdit)return;
    HWND edit=c.textEdit;
    if(accept && c.textIndex && *c.textIndex<c.document.items.size()) {
        auto& item=c.document.items[*c.textIndex];
        item.text=CaptureEditText();
        if(item.text.find_first_not_of(L" \t\r\n")==std::wstring::npos) {
            c.document.items.erase(c.document.items.begin()+static_cast<std::ptrdiff_t>(*c.textIndex));
            c.document.active.reset();
        } else item.y2=std::min(static_cast<float>(c.selection.bottom),item.y1+CaptureTextHeight(item,item.text));
        c.document.Commit();
    } else c.document.Cancel();
    c.textEdit=nullptr;c.textIndex.reset();c.imeComposing=false;c.textFrame={};
    DestroyWindow(edit);
    c.textFont.reset();
    SetFocus(app.overlay);RefreshToolbar();
}
LRESULT CALLBACK TextEditProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(app.capture && app.capture->textEdit==window) {
        if(message==WM_IME_STARTCOMPOSITION)app.capture->imeComposing=true;
        if(message==WM_IME_ENDCOMPOSITION)app.capture->imeComposing=false;
        if(message==WM_GETDLGCODE)return DefSubclassProc(window,message,w,l)|DLGC_WANTALLKEYS;
        if(message==WM_SETFOCUS || message==WM_KILLFOCUS)InvalidateRect(app.overlay,nullptr,FALSE);
        if(message==WM_PAINT) {
            const auto result=DefSubclassProc(window,message,w,l);
            if(GetWindowTextLengthW(window)==0 && !app.capture->imeComposing) {
                HDC dc=GetDC(window);
                if(dc) {
                    const int saved=SaveDC(dc);SelectObject(dc,app.capture->textFont.get());SetBkMode(dc,TRANSPARENT);
                    SetTextColor(dc,app.palette.highContrast?app.palette.text:RGB(128,128,128));
                    RECT rect{};GetClientRect(window,&rect);
                    DrawTextW(dc,L"输入文字…",-1,&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
                    RestoreDC(dc,saved);ReleaseDC(window,dc);
                }
            }
            return result;
        }
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,TextEditProc,1);
    return DefSubclassProc(window,message,w,l);
}
void StartTextEdit(std::size_t index) {
    auto& c=*app.capture;
    CloseToolPalette();CloseStylePalette();
    if(c.textEdit)EndTextEdit(true);
    if(index>=c.document.items.size())return;
    c.document.Begin();c.document.active=index;c.textIndex=index;
    SyncStyleFromActive();
    auto& item=c.document.items[index];
    c.textFont.reset(CreateFontW(-static_cast<int>(std::lround(item.fontSize)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"));
    c.textEdit=CreateWindowExW(0,L"EDIT",item.text.c_str(),
        WS_CHILD|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
        static_cast<int>(item.x1),static_cast<int>(item.y1),std::max(20,static_cast<int>(item.x2-item.x1)),
        std::max(24,static_cast<int>(item.y2-item.y1)),app.overlay,reinterpret_cast<HMENU>(700),app.instance,nullptr);
    if(!c.textEdit) {
        c.document.Cancel();c.textIndex.reset();c.textFont.reset();
        Notify(L"无法创建文字输入框。",true);return;
    }
    SendMessageW(c.textEdit,WM_SETFONT,reinterpret_cast<WPARAM>(c.textFont.get()),TRUE);
    SendMessageW(c.textEdit,EM_SETLIMITTEXT,4096,0);
    SendMessageW(c.textEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
    RECT format{};GetClientRect(c.textEdit,&format);SendMessageW(c.textEdit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
    SetWindowSubclass(c.textEdit,TextEditProc,1,0);
    UpdateTextEditorSize();ShowWindow(c.textEdit,SW_SHOW);
    SendMessageW(c.textEdit,EM_SETSEL,0,-1);SetFocus(c.textEdit);
    RefreshToolbar();
}
void SetCaptureTool(CaptureTool tool) {
    auto& c=*app.capture;
    CloseToolPalette();CloseStylePalette();
    EndTextEdit(true);
    c.document.active.reset();c.tool=tool;
    if(tool==CaptureTool::Redact)c.color=c.redactionColor;
    else if(tool!=CaptureTool::Select && tool!=CaptureTool::Crop)c.color=c.drawingColor;
    SetFocus(app.overlay);RefreshToolbar();
}
void SetMosaicTool(bool blur) {
    auto& c=*app.capture;EndTextEdit(true);
    CloseToolPalette();CloseStylePalette();
    const auto tool=blur?CaptureTool::Blur:CaptureTool::Mosaic;
    if(c.document.active && c.document.items[*c.document.active].kind==imago::AnnotationKind::Mosaic) {
        c.document.Begin();c.document.items[*c.document.active].mosaicStyle=blur?imago::MosaicStyle::Blur:imago::MosaicStyle::Pixelate;
        c.document.Commit();c.tool=tool;SetFocus(app.overlay);RefreshToolbar();
    }else SetCaptureTool(tool);
}
#include "capture_extras.inl"
struct CaptureModalScope {
    bool previous{app.modal};
    CaptureModalScope(){app.modal=true;}
    ~CaptureModalScope(){app.modal=previous;}
};
void ApplyCaptureSize(UINT index) {
    auto& c=*app.capture;const auto style=CurrentCaptureStyle();
    if(index>=5 || style.redact)return;
    const float value=style.Values()[index]*(style.mosaic?1.f:c.scale);
    if(style.mosaic)c.mosaicSize=style.Values()[index];else if(style.text)c.textSize=value;else c.stroke=value;
    if(c.document.active) {
        c.document.Begin();auto& item=c.document.items[*c.document.active];
        if(style.mosaic)item.blockSize=c.mosaicSize;
        else if(style.text) {
            item.fontSize=c.textSize;
            item.y2=std::min(static_cast<float>(c.selection.bottom),std::max(item.y2,item.y1+CaptureTextHeight(item,item.text)));
        }else item.stroke=c.stroke;
        c.document.Commit();
    }
    RefreshToolbar();
}
void ShowCustomColor() {
    auto& c=*app.capture;
    if(c.tooltip)SendMessageW(c.tooltip,TTM_POP,0,0);
    CHOOSECOLORW dialog{sizeof(dialog)};dialog.hwndOwner=app.overlay;dialog.rgbResult=c.color;
    dialog.lpCustColors=app.customColors.data();dialog.Flags=CC_FULLOPEN|CC_RGBINIT|CC_ANYCOLOR;
    BOOL accepted{};
    {CaptureModalScope modal;accepted=ChooseColorW(&dialog);}
    if(accepted)ApplyCaptureColor(dialog.rgbResult);
    else if(CommDlgExtendedError())Notify(L"无法打开系统选色器，请重试。",true);
    SetFocus(c.styleExpanded?GetDlgItem(app.overlay,CustomColorCommand):app.overlay);
}
int SelectionEdges(POINT point) {
    auto& c=*app.capture;
    if(!c.selected)return 0;
    const int tolerance=Px(6,c.scale);
    RECT expanded=c.selection;InflateRect(&expanded,tolerance,tolerance);
    if(!PtInRect(&expanded,point))return 0;
    int edges=0;
    if(std::abs(point.x-c.selection.left)<=tolerance)edges|=1;
    else if(std::abs(point.x-c.selection.right)<=tolerance)edges|=4;
    if(std::abs(point.y-c.selection.top)<=tolerance)edges|=2;
    else if(std::abs(point.y-c.selection.bottom)<=tolerance)edges|=8;
    return edges?edges:PtInRect(&c.selection,point)?16:0;
}
void SetSelectionCursor(POINT point) {
    auto& c=*app.capture;
    if(c.ocrBusy){SetCursor(LoadCursorW(nullptr,IDC_APPSTARTING));return;}
    if(c.pickingColor){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return;}
    if(c.selected && (PtInRect(&c.toolbar,point) || (c.toolsExpanded && PtInRect(&c.toolPalette,point)) ||
        (c.styleExpanded && PtInRect(&c.stylePalette,point)))){SetCursor(LoadCursorW(nullptr,IDC_ARROW));return;}
    LPCWSTR cursor=c.tool==CaptureTool::Select?IDC_ARROW:c.tool==CaptureTool::Text?IDC_IBEAM:IDC_CROSS;
    int edges=0;
    if(c.tool==CaptureTool::Crop)edges=SelectionEdges(point);
    else if(c.document.active) {
        const auto& item=c.document.items[*c.document.active];
        edges=imago::HitAnnotationHandle(item,{static_cast<float>(point.x),static_cast<float>(point.y)},6*c.scale);
        if(edges && item.kind==imago::AnnotationKind::Arrow)cursor=IDC_CROSS;
    }
    switch(edges) {
    case 1:case 4:cursor=IDC_SIZEWE;break;
    case 2:case 8:cursor=IDC_SIZENS;break;
    case 3:case 12:cursor=IDC_SIZENWSE;break;
    case 6:case 9:cursor=IDC_SIZENESW;break;
    case 16:cursor=IDC_SIZEALL;break;
    }
    SetCursor(LoadCursorW(nullptr,cursor));
}
void PaintActiveAnnotation(Capture& c) {
    if(!c.document.active || c.textEdit || c.pickingColor || c.ocrBusy || c.tool==CaptureTool::Crop)return;
    const auto& item=c.document.items[*c.document.active];
    auto& canvas=c.canvas;
    auto bounds=imago::AnnotationBounds(item);
    canvas.brush->SetColor(Color(app.palette.accent));
    canvas.target->DrawRectangle(bounds,canvas.brush.Get(),1.f);
    std::vector<D2D1_POINT_2F> handles;
    if(item.kind==imago::AnnotationKind::Arrow)handles={{item.x1,item.y1},{item.x2,item.y2}};
    else handles={{bounds.left,bounds.top},{(bounds.left+bounds.right)/2,bounds.top},{bounds.right,bounds.top},
        {bounds.left,(bounds.top+bounds.bottom)/2},{bounds.right,(bounds.top+bounds.bottom)/2},
        {bounds.left,bounds.bottom},{(bounds.left+bounds.right)/2,bounds.bottom},{bounds.right,bounds.bottom}};
    for(auto point:handles) {
        const float radius=3*c.scale;
        auto rect=D2D1::RectF(point.x-radius,point.y-radius,point.x+radius,point.y+radius);
        canvas.Fill(rect,Color(app.palette.surface));canvas.brush->SetColor(Color(app.palette.accent));
        canvas.target->DrawRectangle(rect,canvas.brush.Get());
    }
}
void PaintCapture(HWND window) {
    PAINTSTRUCT ps{};BeginPaint(window,&ps);
    auto& c=*app.capture;auto& canvas=c.canvas;
    if(!canvas.Ensure(window,c.desktop,app.factory.Get())) {
        EndPaint(window,&ps);PostMessageW(window,WM_CLOSE,0,0);Notify(L"无法创建截图画布，请检查显示驱动或屏幕总尺寸。",true);return;
    }
    canvas.target->BeginDraw();
    const float width=static_cast<float>(c.desktop.width),height=static_cast<float>(c.desktop.height);
    canvas.target->DrawBitmap(canvas.bitmap.Get(),D2D1::RectF(0,0,width,height),1.f,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    const auto dim=D2D1::ColorF(0.f,0.f,0.f,.43f);
    const bool hover=!c.selected && !c.dragging && c.hoverWindow.has_value();
    if(c.selected || c.dragging || hover) {
        const RECT displayed=hover?*c.hoverWindow:c.selection;
        const auto rect=RectF(displayed);
        if(!c.recordingMode) {
            canvas.target->PushAxisAlignedClip(rect,D2D1_ANTIALIAS_MODE_ALIASED);
            const HRESULT hr=imago::DrawAnnotations(canvas.target.Get(),app.writeFactory.Get(),c.desktop,c.mosaics,c.document.items,c.textIndex);
            PaintActiveAnnotation(c);canvas.target->PopAxisAlignedClip();
            if(FAILED(hr))PostMessageW(window,WM_CLOSE,0,0);
        }
        canvas.Fill(D2D1::RectF(0,0,width,rect.top),dim);
        canvas.Fill(D2D1::RectF(0,rect.bottom,width,height),dim);
        canvas.Fill(D2D1::RectF(0,rect.top,rect.left,rect.bottom),dim);
        canvas.Fill(D2D1::RectF(rect.right,rect.top,width,rect.bottom),dim);
        canvas.brush->SetColor(Color(app.palette.accent));canvas.target->DrawRectangle(rect,canvas.brush.Get(),2.f);
        if(c.selected && c.tool==CaptureTool::Crop && !c.dragging && !c.pickingColor && !c.ocrBusy) {
            const float radius=3*c.scale;
            for(const auto& point:std::vector<D2D1_POINT_2F>{{rect.left,rect.top},{(rect.left+rect.right)/2,rect.top},{rect.right,rect.top},
                {rect.left,(rect.top+rect.bottom)/2},{rect.right,(rect.top+rect.bottom)/2},
                {rect.left,rect.bottom},{(rect.left+rect.right)/2,rect.bottom},{rect.right,rect.bottom}}) {
                auto box=D2D1::RectF(point.x-radius,point.y-radius,point.x+radius,point.y+radius);
                canvas.Fill(box,Color(app.palette.surface));canvas.brush->SetColor(Color(app.palette.accent));canvas.target->DrawRectangle(box,canvas.brush.Get());
            }
        }
        const auto dimensions=std::to_wstring(displayed.right-displayed.left)+L" × "+std::to_wstring(displayed.bottom-displayed.top);
        const float labelW=132*c.scale,labelH=28*c.scale;
        const float x=std::clamp(rect.left,0.f,std::max(0.f,width-labelW)),y=rect.top>=labelH+6?rect.top-labelH-6:rect.top+6;
        canvas.Round(D2D1::RectF(x,y,x+labelW,y+labelH),4*c.scale,Color(app.palette.surface),Color(app.palette.border));
        canvas.Text(dimensions,D2D1::RectF(x+8*c.scale,y,x+labelW-4,y+labelH),12*c.scale,app.palette.text,app.writeFactory.Get());
        if(c.textEdit) {
            canvas.Round(RectF(c.textFrame),6*c.scale,Color(TextEditorBackground()),Color(app.palette.border));
            if(GetFocus()==c.textEdit) {
                const float focusY=c.textFrame.bottom-1.f*c.scale;
                canvas.brush->SetColor(Color(app.palette.accent));
                canvas.target->DrawLine({c.textFrame.left+6*c.scale,focusY},{c.textFrame.right-6*c.scale,focusY},canvas.brush.Get(),2*c.scale);
            }
        }
        if(c.selected && !c.dragging && !c.pickingColor && !c.recordingMode && c.annotationDrag==AnnotationDrag::None) {
            canvas.Round(RectF(c.toolbar),8*c.scale,Color(app.palette.background),Color(app.palette.border));
            canvas.brush->SetColor(Color(app.palette.border));
            for(float offset:{82.f,250.f,366.f}) {
                const float dividerX=c.toolbar.left+offset*c.scale;
                canvas.target->DrawLine({dividerX,c.toolbar.top+14*c.scale},{dividerX,c.toolbar.bottom-14*c.scale},canvas.brush.Get());
            }
            if(c.toolsExpanded || c.toolsMotion.running)canvas.Round(RectF(c.toolPalette),8*c.scale,Color(app.palette.background),Color(app.palette.border));
            if(c.styleExpanded || c.styleMotion.running) {
                const auto style=CurrentCaptureStyle();const float panelX=static_cast<float>(c.stylePalette.left),panelY=static_cast<float>(c.stylePalette.top),s=c.scale;
                canvas.Round(RectF(c.stylePalette),8*s,Color(app.palette.background),Color(app.palette.border));
                canvas.Text(style.Label(),D2D1::RectF(panelX+12*s,panelY+8*s,panelX+292*s,panelY+28*s),12*s,app.palette.text,app.writeFactory.Get());
                canvas.brush->SetColor(Color(app.palette.border));
                canvas.target->DrawLine({panelX+12*s,panelY+76*s},{panelX+292*s,panelY+76*s},canvas.brush.Get());
                canvas.Text(style.mosaic?L"颜色 · 用于后续绘图":L"颜色",D2D1::RectF(panelX+12*s,panelY+84*s,panelX+292*s,panelY+104*s),12*s,app.palette.text,app.writeFactory.Get());
            }
        }
    } else canvas.Fill(D2D1::RectF(0,0,width,height),dim);
    if(!c.selected && !c.dragging) {
        POINT pointer{c.pointer.x+c.bounds.left,c.pointer.y+c.bounds.top};
        RECT work=MonitorWork(pointer);OffsetRect(&work,-c.bounds.left,-c.bounds.top);
        const float boxW=std::min(430*c.scale,static_cast<float>(work.right-work.left)-32);
        const float x=(work.left+work.right-boxW)/2,y=work.top+28*c.scale;
        canvas.Round(D2D1::RectF(x,y,x+boxW,y+44*c.scale),8*c.scale,Color(app.palette.surface),Color(app.palette.border));
        canvas.Text(app.settings.windowSnap?L"单击截取窗口 · 拖动框选 · Shift 跳过吸附 · Esc 取消":L"拖动框选 · 标注后 Enter 复制 · Ctrl+S 保存 · Esc 取消",
            D2D1::RectF(x+14*c.scale,y,x+boxW-10,y+44*c.scale),12*c.scale,app.palette.text,app.writeFactory.Get());
    }
    if(c.pickingColor)PaintColorPicker(c);
    if(!c.pickingColor && !c.dragging && c.annotationDrag==AnnotationDrag::None)PaintCaptureOcrStatus(c);
    canvas.End();EndPaint(window,&ps);
}
void AnnotationPointerDown(POINT point) {
    auto& c=*app.capture;
    EndTextEdit(true);
    if(!PtInRect(&c.selection,point)){c.document.active.reset();RefreshToolbar();return;}
    D2D1_POINT_2F p{static_cast<float>(point.x),static_cast<float>(point.y)};
    int handle=0;
    if(c.document.active)handle=imago::HitAnnotationHandle(c.document.items[*c.document.active],p,6*c.scale);
    auto hit=handle?c.document.active:imago::HitAnnotation(c.document.items,p,6*c.scale);
    if(hit) {
        c.document.active=hit;SyncStyleFromActive();
        if(!handle && c.tool==CaptureTool::Text && c.document.items[*hit].kind==imago::AnnotationKind::Text) {
            StartTextEdit(*hit);return;
        }
        c.document.Begin();c.originalItem=c.document.items[*hit];c.annotationHandle=handle;
        c.annotationDrag=handle?AnnotationDrag::Resize:AnnotationDrag::Move;
    } else {
        c.document.active.reset();
        if(c.tool==CaptureTool::Select){RefreshToolbar();return;}
        if(c.document.items.size()>=512){Notify(L"当前截图的标注已达 512 个，请删除部分对象后继续。",true);return;}
        c.document.Begin();
        imago::Annotation item;
        item.x1=item.x2=p.x;item.y1=item.y2=p.y;item.color=c.color;item.stroke=c.stroke;item.fontSize=c.textSize;item.blockSize=c.mosaicSize;
        switch(c.tool) {
        case CaptureTool::Rectangle:item.kind=imago::AnnotationKind::Rectangle;break;
        case CaptureTool::Ellipse:item.kind=imago::AnnotationKind::Ellipse;break;
        case CaptureTool::Arrow:item.kind=imago::AnnotationKind::Arrow;break;
        case CaptureTool::Text:item.kind=imago::AnnotationKind::Text;break;
        case CaptureTool::Redact:item.kind=imago::AnnotationKind::Redact;break;
        case CaptureTool::Mosaic:item.kind=imago::AnnotationKind::Mosaic;break;
        case CaptureTool::Blur:item.kind=imago::AnnotationKind::Mosaic;item.mosaicStyle=imago::MosaicStyle::Blur;break;
        default:c.document.Cancel();return;
        }
        if(item.kind==imago::AnnotationKind::Text) {
            item.x1=std::max(static_cast<float>(c.selection.left),std::min(p.x,c.selection.right-160*c.scale));
            item.y1=std::max(static_cast<float>(c.selection.top),std::min(p.y,c.selection.bottom-64*c.scale));
            item.x2=std::min(static_cast<float>(c.selection.right),item.x1+320*c.scale);
            item.y2=std::min(static_cast<float>(c.selection.bottom),item.y1+item.fontSize*1.5f+2*c.scale);
        }
        c.document.items.push_back(std::move(item));c.document.active=c.document.items.size()-1;
        if(c.tool==CaptureTool::Text){StartTextEdit(*c.document.active);return;}
        c.annotationDrag=AnnotationDrag::Create;
    }
    c.anchor=point;SetFocus(app.overlay);SetCapture(app.overlay);HideToolbar();RefreshToolbar();
}
void AnnotationPointerMove(POINT point) {
    auto& c=*app.capture;
    if(!c.document.active || c.annotationDrag==AnnotationDrag::None)return;
    auto& item=c.document.items[*c.document.active];
    const float x=static_cast<float>(std::clamp(point.x,c.selection.left,c.selection.right));
    const float y=static_cast<float>(std::clamp(point.y,c.selection.top,c.selection.bottom));
    if(c.annotationDrag==AnnotationDrag::Create){
        item.x2=x;item.y2=y;
        if((GetKeyState(VK_SHIFT)&0x8000) && (item.kind==imago::AnnotationKind::Rectangle || item.kind==imago::AnnotationKind::Ellipse)) {
            const float dx=x-item.x1,dy=y-item.y1;
            const float size=std::min({std::max(std::abs(dx),std::abs(dy)),
                dx<0?item.x1-c.selection.left:c.selection.right-item.x1,
                dy<0?item.y1-c.selection.top:c.selection.bottom-item.y1});
            item.x2=item.x1+(dx<0?-size:size);item.y2=item.y1+(dy<0?-size:size);
        }
    }
    else if(c.annotationDrag==AnnotationDrag::Move) {
        const auto bounds=imago::AnnotationBounds(c.originalItem);
        const float dx=std::clamp(static_cast<float>(point.x-c.anchor.x),-bounds.left,c.desktop.width-bounds.right);
        const float dy=std::clamp(static_cast<float>(point.y-c.anchor.y),-bounds.top,c.desktop.height-bounds.bottom);
        item=c.originalItem;item.x1+=dx;item.x2+=dx;item.y1+=dy;item.y2+=dy;
    } else if(item.kind==imago::AnnotationKind::Arrow) {
        if(c.annotationHandle==1){item.x1=x;item.y1=y;}else{item.x2=x;item.y2=y;}
    } else {
        const auto bounds=imago::AnnotationBounds(c.originalItem);
        item.x1=bounds.left;item.y1=bounds.top;item.x2=bounds.right;item.y2=bounds.bottom;
        if(c.annotationHandle&1)item.x1=std::min(x,item.x2-2);
        if(c.annotationHandle&2)item.y1=std::min(y,item.y2-2);
        if(c.annotationHandle&4)item.x2=std::max(x,item.x1+2);
        if(c.annotationHandle&8)item.y2=std::max(y,item.y1+2);
    }
    InvalidateRect(app.overlay,nullptr,FALSE);
}
void EndAnnotationDrag(bool accept) {
    auto& c=*app.capture;
    if(c.annotationDrag==AnnotationDrag::None)return;
    if(accept && c.document.active) {
        const auto& item=c.document.items[*c.document.active];auto bounds=imago::AnnotationBounds(item);
        if(item.kind==imago::AnnotationKind::Arrow)accept=std::hypot(item.x2-item.x1,item.y2-item.y1)>=2;
        else accept=bounds.right-bounds.left>=2 && bounds.bottom-bounds.top>=2;
    }
    if(accept)c.document.Commit();else c.document.Cancel();
    c.annotationDrag=AnnotationDrag::None;ReleaseCapture();LayoutToolbar();RefreshToolbar();
}
void CaptureCommandAction(UINT command) {
    if(app.modal || !app.capture)return;
    auto& c=*app.capture;
    if(c.recordingMode)return;
    if(c.ocrBusy){if(command==CancelCommand)CancelOcr();return;}
    if(c.dragging || c.pickingColor || c.annotationDrag!=AnnotationDrag::None)return;
    if(command==CancelCommand){CloseCapture(true);return;}
    EndTextEdit(true);
    if(command>=FirstSizeCommand && command<FirstSizeCommand+5){ApplyCaptureSize(command-FirstSizeCommand);return;}
    if(command>=FirstColorCommand && command<FirstColorCommand+std::size(CaptureColors)){ApplyCaptureColor(CaptureColors[command-FirstColorCommand]);return;}
    if(command==CustomColorCommand){ShowCustomColor();return;}
    if(command!=ToolsCommand)CloseToolPalette();
    if(command!=ColorCommand)CloseStylePalette();
    switch(command) {
    case CopyCommand:FinishCapture(false);return;
    case PinCommand:FinishCapture(true);return;
    case SaveCommand:SaveCapture();return;
    case OcrCommand:RecognizeCaptureText();return;
    case ScrollCommand:StartScrollCapture();return;
    case RecordingCommand:EnterRecordingMode();return;
    case ScreenshotModeCommand:return;
    case SelectToolCommand:SetCaptureTool(CaptureTool::Select);return;
    case RectangleToolCommand:SetCaptureTool(CaptureTool::Rectangle);return;
    case EllipseToolCommand:SetCaptureTool(CaptureTool::Ellipse);return;
    case ArrowToolCommand:SetCaptureTool(CaptureTool::Arrow);return;
    case TextToolCommand:SetCaptureTool(CaptureTool::Text);return;
    case RedactToolCommand:SetCaptureTool(CaptureTool::Redact);return;
    case MosaicToolCommand:SetMosaicTool(false);return;
    case BlurToolCommand:SetMosaicTool(true);return;
    case CropToolCommand:SetCaptureTool(CaptureTool::Crop);return;
    case ToolsCommand:ToggleToolPalette();return;
    case ColorCommand:ToggleStylePalette();return;
    case PickerCommand:StartColorPicker();return;
    case UndoCommand:c.document.Undo();break;
    case RedoCommand:c.document.Redo();break;
    case DeleteCommand:c.document.DeleteActive();break;
    default:return;
    }
    RefreshToolbar();SetFocus(app.overlay);
}
LRESULT CALLBACK OverlayProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(!app.capture)return DefWindowProcW(window,message,w,l);
    auto& c=*app.capture;
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:PaintCapture(window);return 0;
    case WM_TIMER:if(w==PaletteMotionTimer){TickPaletteMotion();return 0;}break;
    case WM_DRAWITEM:PaintButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_NOTIFY:{
        const auto* header=reinterpret_cast<NMHDR*>(l);
        if(header->hwndFrom==c.tooltip && header->code==TTN_GETDISPINFOW) {
            auto* info=reinterpret_cast<NMTTDISPINFOW*>(l);
            GetWindowTextW(reinterpret_cast<HWND>(info->hdr.idFrom),info->szText,static_cast<int>(std::size(info->szText)));
            info->lpszText=info->szText;return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT:{
        HDC dc=reinterpret_cast<HDC>(w);const auto background=TextEditorBackground();
        SetTextColor(dc,app.palette.highContrast?app.palette.text:c.color);SetBkColor(dc,background);
        SetDCBrushColor(dc,background);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_SETCURSOR:
        if(reinterpret_cast<HWND>(w)==window && LOWORD(l)==HTCLIENT){POINT p{};GetCursorPos(&p);ScreenToClient(window,&p);SetSelectionCursor(p);return TRUE;}break;
    case WM_LBUTTONDBLCLK:{
        if(!c.selected || c.pickingColor || c.ocrBusy || c.tool==CaptureTool::Crop)return 0;
        const POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(PtInRect(&c.toolbar,point) || (c.toolsExpanded && PtInRect(&c.toolPalette,point)) ||
            (c.styleExpanded && PtInRect(&c.stylePalette,point)))return 0;
        if(c.textEdit && PtInRect(&c.textFrame,point)){SetFocus(c.textEdit);return 0;}
        CloseToolPalette();CloseStylePalette();
        EndAnnotationDrag(true);
        auto index=imago::HitAnnotation(c.document.items,{static_cast<float>(GET_X_LPARAM(l)),static_cast<float>(GET_Y_LPARAM(l))},6*c.scale);
        if(index && c.document.items[*index].kind==imago::AnnotationKind::Text)StartTextEdit(*index);
        return 0;
    }
    case WM_LBUTTONDOWN:{
        if(app.modal || c.ocrBusy || app.recorder)return 0;
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(c.pickingColor){c.pointer=point;EndColorPicker(true);return 0;}
        if(c.toolsExpanded && PtInRect(&c.toolPalette,point))return 0;
        if(c.styleExpanded && PtInRect(&c.stylePalette,point))return 0;
        if(c.textEdit && PtInRect(&c.textFrame,point)){SetFocus(c.textEdit);return 0;}
        CloseToolPalette();CloseStylePalette();
        if(c.selected && PtInRect(&c.toolbar,point))return 0;
        if(c.selected && c.tool!=CaptureTool::Crop){AnnotationPointerDown(point);return 0;}
        EndTextEdit(true);c.document.active.reset();
        c.pendingSnap=!c.selected && app.settings.windowSnap && !(GetKeyState(VK_SHIFT)&0x8000)
            ?imago::WindowAt(c.windows,point):std::optional<RECT>{};
        c.edges=SelectionEdges(point);c.original=c.selection;c.anchor=point;c.dragging=true;c.pointer=point;
        if(!c.edges){c.selected=false;c.selection={point.x,point.y,point.x,point.y};}
        HideToolbar();SetFocus(window);SetCapture(window);InvalidateRect(window,nullptr,FALSE);return 0;
    }
    case WM_MOUSEMOVE:{
        c.pointer={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(c.pickingColor){c.pickerCopied=false;InvalidateRect(window,nullptr,FALSE);return 0;}
        if(c.annotationDrag!=AnnotationDrag::None){AnnotationPointerMove(c.pointer);return 0;}
        if(!c.dragging){if(!c.selected)UpdateWindowHover();return 0;}
        if(std::abs(c.pointer.x-c.anchor.x)>=GetSystemMetrics(SM_CXDRAG) ||
           std::abs(c.pointer.y-c.anchor.y)>=GetSystemMetrics(SM_CYDRAG))c.pendingSnap.reset();
        if(c.edges==16)c.selection=imago::MoveRectWithin(c.original,c.pointer.x-c.anchor.x,c.pointer.y-c.anchor.y,c.desktop.width,c.desktop.height);
        else if(c.edges){
            RECT rect=c.original;
            if(c.edges&1)rect.left=c.pointer.x;if(c.edges&2)rect.top=c.pointer.y;
            if(c.edges&4)rect.right=c.pointer.x;if(c.edges&8)rect.bottom=c.pointer.y;
            c.selection=imago::NormalizeRect({rect.left,rect.top},{rect.right,rect.bottom},c.desktop.width,c.desktop.height);
        }else c.selection=imago::NormalizeRect(c.anchor,c.pointer,c.desktop.width,c.desktop.height);
        InvalidateRect(window,nullptr,FALSE);return 0;
    }
    case WM_LBUTTONUP:
        if(c.annotationDrag!=AnnotationDrag::None){AnnotationPointerMove({GET_X_LPARAM(l),GET_Y_LPARAM(l)});EndAnnotationDrag(true);return 0;}
        if(c.dragging){
            const POINT released{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            if(std::abs(released.x-c.anchor.x)>=GetSystemMetrics(SM_CXDRAG) ||
               std::abs(released.y-c.anchor.y)>=GetSystemMetrics(SM_CYDRAG))c.pendingSnap.reset();
            if(c.edges==0)c.selection=imago::NormalizeRect(c.anchor,released,c.desktop.width,c.desktop.height);
            if(c.pendingSnap && !(GetKeyState(VK_SHIFT)&0x8000))c.selection=*c.pendingSnap;
            c.pendingSnap.reset();c.hoverWindow.reset();
            c.dragging=false;c.selected=c.selection.right>c.selection.left && c.selection.bottom>c.selection.top;
            ReleaseCapture();LayoutToolbar();InvalidateRect(window,nullptr,FALSE);
        }return 0;
    case WM_CAPTURECHANGED:
        if(c.annotationDrag!=AnnotationDrag::None)EndAnnotationDrag(false);
        if(c.dragging){c.dragging=false;c.selection=c.original;c.selected=c.selection.right>c.selection.left && c.selection.bottom>c.selection.top;LayoutToolbar();InvalidateRect(window,nullptr,FALSE);}
        return 0;
    case WM_RBUTTONUP:
        if(c.recordingMode && !app.recorder){CloseCapture(true);return 0;}
        if(c.toolsExpanded){CloseToolPalette(true);return 0;}
        if(c.styleExpanded){CloseStylePalette(true);return 0;}
        if(c.ocrBusy){CancelOcr();return 0;}
        if(c.pickingColor){EndColorPicker(false);return 0;}
        if(c.textEdit){EndTextEdit(false);return 0;}
        if(c.annotationDrag!=AnnotationDrag::None){EndAnnotationDrag(false);return 0;}
        CloseCapture(true);return 0;
    case WM_CLOSE:if(!app.modal)CloseCapture(true);return 0;
    case WM_DISPLAYCHANGE:
        if(app.recorder){app.recorder->Stop();return 0;}
        if(!app.modal){CloseCapture(true);Notify(L"显示器配置已改变，请重新截图。");}return 0;
    case WM_COMMAND:
        // EDIT sends EN_CHANGE/EN_SETFOCUS through WM_COMMAND; these are not toolbar clicks.
        if(LOWORD(w)==700 && HIWORD(w)==EN_CHANGE)UpdateTextEditorSize();
        else if(LOWORD(w)!=700 && HIWORD(w)==BN_CLICKED)CaptureCommandAction(LOWORD(w));
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
bool CaptureKey(const MSG& message) {
    if(!app.overlay || !app.capture || app.modal || GetAncestor(message.hwnd,GA_ROOT)!=app.overlay)return false;
    auto& c=*app.capture;
    if((message.message==WM_KEYDOWN || message.message==WM_KEYUP) && message.wParam==VK_SHIFT && !c.selected)UpdateWindowHover();
    if(message.message!=WM_KEYDOWN)return false;
    if(c.recordingMode) {
        if(app.recorder)return true;
        if(message.wParam==VK_ESCAPE){CloseCapture(true);return true;}
        if(c.dragging)return true;
        if(message.wParam==VK_RETURN){HandleRecordingAction(imago::RecordingAction::Start);return true;}
        if(message.wParam==VK_TAB){ShowRecordingPanel();return true;}
        if(!c.selected)return true;
        const int step=(GetKeyState(VK_SHIFT)&0x8000)?10:1;
        int dx=0,dy=0;
        if(message.wParam==VK_LEFT)dx=-step;else if(message.wParam==VK_RIGHT)dx=step;
        else if(message.wParam==VK_UP)dy=-step;else if(message.wParam==VK_DOWN)dy=step;else return true;
        c.selection=imago::MoveRectWithin(c.selection,dx,dy,c.desktop.width,c.desktop.height);
        LayoutToolbar();InvalidateRect(app.overlay,nullptr,FALSE);return true;
    }
    if(message.wParam==VK_TAB || message.wParam==VK_LEFT || message.wParam==VK_RIGHT ||
       message.wParam==VK_UP || message.wParam==VK_DOWN || message.wParam==VK_F4 || message.wParam==VK_SPACE || message.wParam==VK_RETURN) {
        c.keyboardNavigation=true;
        SendMessageW(app.overlay,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
    }
    bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(c.ocrBusy) {
        if(message.wParam==VK_ESCAPE || (message.wParam==VK_RETURN && GetDlgCtrlID(message.hwnd)==CancelCommand))CancelOcr();
        // Let the native cancel button receive Space/Tab. All editing keys stay disabled.
        return message.wParam!=VK_TAB && message.wParam!=VK_SPACE;
    }
    if(c.pickingColor) {
        if(message.wParam==VK_ESCAPE)EndColorPicker(false);
        else if(message.wParam==VK_RETURN)EndColorPicker(true);
        else if(control && message.wParam=='C') {
            std::wstring error;c.pickerCopied=imago::CopyText(app.controller,HexColor(SampleColor()),error);
            if(!c.pickerCopied)Notify(error.c_str(),true);InvalidateRect(app.overlay,nullptr,FALSE);
        }
        return true;
    }
    if(c.textEdit && message.hwnd==c.textEdit) {
        if(c.imeComposing)return false;
        if(message.wParam==VK_ESCAPE){EndTextEdit(false);return true;}
        if(message.wParam==VK_RETURN && control){EndTextEdit(true);return true;}
        if(message.wParam=='S' && control){EndTextEdit(true);SaveCapture();return true;}
        if(message.wParam==VK_TAB){EndTextEdit(true);return true;}
        return false; // Leave IME, text selection, newlines and the edit control's local undo intact.
    }
    if(message.wParam==VK_ESCAPE) {
        if(c.toolsExpanded){CloseToolPalette(true);return true;}
        if(c.styleExpanded){CloseStylePalette(true);return true;}
        if(c.annotationDrag!=AnnotationDrag::None)EndAnnotationDrag(false);else CloseCapture(true);
        return true;
    }
    if(!c.selected || c.dragging || c.annotationDrag!=AnnotationDrag::None)return false;
    const auto focusedId=static_cast<UINT>(GetDlgCtrlID(message.hwnd));
    if(c.toolsExpanded && !control && (message.wParam==VK_LEFT || message.wParam==VK_RIGHT)) {
        auto found=std::find(c.toolButtons.begin(),c.toolButtons.end(),message.hwnd);
        if(found!=c.toolButtons.end()) {
            const auto count=static_cast<std::ptrdiff_t>(c.toolButtons.size());
            const auto next=(found-c.toolButtons.begin()+(message.wParam==VK_RIGHT?1:count-1))%count;
            SetFocus(c.toolButtons[static_cast<std::size_t>(next)]);return true;
        }
    }
    if(c.styleExpanded && !control && (message.wParam==VK_LEFT || message.wParam==VK_RIGHT || message.wParam==VK_UP || message.wParam==VK_DOWN)) {
        auto found=std::find(c.styleButtons.begin(),c.styleButtons.end(),message.hwnd);
        if(found!=c.styleButtons.end()) {
            int index=static_cast<int>(found-c.styleButtons.begin());
            const bool horizontal=message.wParam==VK_LEFT || message.wParam==VK_RIGHT;
            if(horizontal) {
                const int begin=index<5?0:index<13?5:13,count=index<5?5:index<13?8:2;
                index=begin+(index-begin+(message.wParam==VK_RIGHT?1:count-1))%count;
            }else {
                const bool down=message.wParam==VK_DOWN;
                index=down?(index<5?5+index:index<13?13+(index-5)/4:0):(index>=13?5+(index-13)*4:index>=5?std::min(index-5,4):13);
            }
            if(!IsWindowEnabled(c.styleButtons[index]))index=5;
            SetFocus(c.styleButtons[index]);return true;
        }
    }
    if((message.wParam==VK_UP || message.wParam==VK_DOWN || message.wParam==VK_F4) && (focusedId==ToolsCommand || focusedId==ColorCommand)) {
        CaptureCommandAction(focusedId);return true;
    }
    if(control) {
        if(message.wParam=='S' && shift){StartScrollCapture();return true;}
        if(message.wParam=='Z'){CaptureCommandAction(shift?RedoCommand:UndoCommand);return true;}
        if(message.wParam=='Y'){CaptureCommandAction(RedoCommand);return true;}
        if(message.wParam=='S'){SaveCapture();return true;}
        if(message.wParam=='C'){if(shift)RecognizeCaptureText();else FinishCapture(false);return true;}
        return false;
    }
    if(message.wParam==VK_RETURN) {
        const auto id=static_cast<UINT>(GetDlgCtrlID(message.hwnd));
        CaptureCommandAction(message.hwnd==app.overlay?CopyCommand:id);return true;
    }
    if(message.wParam==VK_DELETE){CaptureCommandAction(DeleteCommand);return true;}
    if(message.wParam=='P'){FinishCapture(true);return true;}
    switch(message.wParam) {
    case 'V':SetCaptureTool(CaptureTool::Select);return true;
    case 'R':SetCaptureTool(CaptureTool::Rectangle);return true;
    case 'O':SetCaptureTool(CaptureTool::Ellipse);return true;
    case 'A':SetCaptureTool(CaptureTool::Arrow);return true;
    case 'T':SetCaptureTool(CaptureTool::Text);return true;
    case 'B':SetCaptureTool(CaptureTool::Redact);return true;
    case 'M':SetMosaicTool(shift);return true;
    case 'E':StartColorPicker();return true;
    case 'C':SetCaptureTool(CaptureTool::Crop);return true;
    }
    int dx=0,dy=0,step=shift?10:1;
    if(message.wParam==VK_LEFT)dx=-step;else if(message.wParam==VK_RIGHT)dx=step;
    else if(message.wParam==VK_UP)dy=-step;else if(message.wParam==VK_DOWN)dy=step;else return false;
    if(c.document.active && c.tool!=CaptureTool::Crop) {
        const auto bounds=imago::AnnotationBounds(c.document.items[*c.document.active]);
        const float x=std::clamp(static_cast<float>(dx),-bounds.left,c.desktop.width-bounds.right);
        const float y=std::clamp(static_cast<float>(dy),-bounds.top,c.desktop.height-bounds.bottom);
        c.document.Begin();auto& item=c.document.items[*c.document.active];item.x1+=x;item.x2+=x;item.y1+=y;item.y2+=y;c.document.Commit();
    }else if(c.tool==CaptureTool::Crop)c.selection=imago::MoveRectWithin(c.selection,dx,dy,c.desktop.width,c.desktop.height);
    LayoutToolbar();RefreshToolbar();return true;
}
void BeginCapture(bool recording) {
    if(app.overlay || app.modal)return;
    std::wstring error;auto capture=std::make_unique<Capture>();
    DwmFlush();capture->desktop=imago::CaptureDesktop(capture->bounds,error);
    if(!capture->desktop.valid()){Notify(error.c_str(),true);return;}
    if(app.settings.windowSnap)capture->windows=imago::SnapshotWindows(capture->bounds);
    POINT cursor{};GetCursorPos(&cursor);capture->pointer={cursor.x-capture->bounds.left,cursor.y-capture->bounds.top};
    app.capture=std::move(capture);const auto bounds=app.capture->bounds;
    app.overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,OverlayClass,L"Imago — 截图与标注",WS_POPUP|WS_CLIPCHILDREN,
        bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,app.instance,nullptr);
    if(!app.overlay){app.capture.reset();Notify(L"无法创建截图窗口。",true);return;}
    ThemeWindow(app.overlay,app.palette,false);
    auto& c=*app.capture;c.scale=Scale(app.overlay);
    RECT work=MonitorWork(cursor);
    // Keep the complete toolbar usable even on a small display or at a large scaling factor.
    c.scale=std::min(c.scale,std::max(.5f,(work.right-work.left-24)/ToolbarWidth));
    c.font.reset(MakeFont(12,c.scale));c.stroke=3*c.scale;c.textSize=24*c.scale;
    const std::pair<UINT,const wchar_t*> buttons[]{
        {ScreenshotModeCommand,L"截图模式（当前）"},{RecordingCommand,L"切换到录屏模式"},
        {ToolsCommand,L"标注工具"},{ColorCommand,L"颜色与样式 · 线粗 / 字号 / 马赛克强度"},
        {UndoCommand,L"撤销 (Ctrl+Z)"},{RedoCommand,L"重做 (Ctrl+Y)"},
        {PickerCommand,L"取色器 (E) · HEX / RGB · Ctrl+C 复制颜色"},
        {ScrollCommand,L"滚动长截图 (Ctrl+Shift+S)"},{OcrCommand,L"识别文字并复制 (Ctrl+Shift+C)"},
        {PinCommand,L"贴图 (P)"},{SaveCommand,L"保存 PNG (Ctrl+S)"},
        {CopyCommand,L"复制 (Enter / Ctrl+C)"},{CancelCommand,L"取消 (Esc)"}};
    c.tooltip=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,app.overlay,nullptr,app.instance,nullptr);
    if(c.tooltip) {
        SendMessageW(c.tooltip,TTM_SETMAXTIPWIDTH,0,Px(360,c.scale));
        SendMessageW(c.tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,450);
        SendMessageW(c.tooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,6000);
    }
    const std::pair<UINT,const wchar_t*> toolButtons[]{
        {ArrowToolCommand,L"箭头 (A)"},{RectangleToolCommand,L"矩形 (R) · Shift 绘制正方形"},
        {EllipseToolCommand,L"圆形 / 椭圆 (O) · Shift 绘制正圆"},{TextToolCommand,L"文本 (T)"},
        {RedactToolCommand,L"实色遮盖 (B)"},{MosaicToolCommand,L"像素马赛克 (M)"},
        {BlurToolCommand,L"模糊马赛克 (Shift+M)"},{SelectToolCommand,L"选择标注 (V)"},
        {CropToolCommand,L"调整选区 (C)"}};
    auto addButton=[&](UINT id,const wchar_t* text,std::vector<HWND>& group) {
        HWND button=Button(app.overlay,text,id,c.font.get());group.push_back(button);
        SetWindowSubclass(button,ToolbarButtonProc,2,0);
        if(c.tooltip && button) {
            TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
            info.hwnd=app.overlay;info.uId=reinterpret_cast<UINT_PTR>(button);info.lpszText=LPSTR_TEXTCALLBACKW;
            SendMessageW(c.tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
        }
    };
    for(const auto& [id,text]:buttons)addButton(id,text,c.buttons);
    for(const auto& [id,text]:toolButtons)addButton(id,text,c.toolButtons);
    for(UINT i=0;i<5;++i)addButton(FirstSizeCommand+i,L"线粗",c.styleButtons);
    for(UINT i=0;i<std::size(CaptureColors);++i)addButton(FirstColorCommand+i,CaptureColorNames[i],c.styleButtons);
    addButton(CustomColorCommand,L"自定义颜色…",c.styleButtons);
    addButton(DeleteCommand,L"删除选中标注 (Delete)",c.styleButtons);
    HideToolbar();UpdateWindowHover();ShowWindow(app.overlay,SW_SHOW);SetForegroundWindow(app.overlay);SetFocus(app.overlay);
    if(recording)EnterRecordingMode();
}
void CloseCapture(bool restoreFocus) {
    if(app.recorder){app.recorder->Stop();return;}
    app.recordingPanel.reset();
    EndScrollCapture(true,L"",true);
    if(app.ocrSource==app.overlay && app.overlay)CancelOcr(false);
    HWND previous=app.previousForeground;HWND window=app.overlay;app.overlay=nullptr;
    if(window)DestroyWindow(window);app.capture.reset();
    if(restoreFocus && previous && IsWindow(previous) && IsWindowVisible(previous))SetForegroundWindow(previous);
}
Image CaptureOutput(std::wstring& error) {
    auto& c=*app.capture;EndTextEdit(true);
    return imago::Composite(c.desktop,c.selection,c.document.items,app.factory.Get(),app.writeFactory.Get(),c.mosaics,error);
}
void FinishCapture(bool pin) {
    if(app.modal || !app.capture || !app.capture->selected || app.capture->ocrBusy || app.capture->dragging || app.capture->annotationDrag!=AnnotationDrag::None)return;
    auto& c=*app.capture;std::wstring error;Image image=CaptureOutput(error);
    if(!image.valid()){Notify(error.c_str(),true);return;}
    POINT location{c.bounds.left+c.selection.left,c.bounds.top+c.selection.top};
    if(pin) {
        if(!CreatePin(std::move(image),location)){Notify(L"无法创建贴图窗口。",true);return;}
        CloseCapture(false);
    }else {
        if(!imago::CopyImage(app.controller,image,error)){Notify(error.c_str(),true);return;}
        CloseCapture(true);
    }
}
void SaveCapture() {
    if(app.modal || !app.capture || !app.capture->selected || app.capture->ocrBusy || app.capture->dragging || app.capture->annotationDrag!=AnnotationDrag::None)return;
    std::wstring error;Image image=CaptureOutput(error);
    if(!image.valid()){Notify(error.c_str(),true);return;}
    imago::SaveResult result;
    {
        struct ModalFlag { ModalFlag(){app.modal=true;} ~ModalFlag(){app.modal=false;} } modal;
        result=imago::SavePngDialog(app.overlay,image,error);
    }
    if(result==imago::SaveResult::Saved)CloseCapture(true);
    else {
        SetForegroundWindow(app.overlay);SetFocus(app.overlay);
        if(result==imago::SaveResult::Failed)Notify(error.c_str(),true);
    }
}
