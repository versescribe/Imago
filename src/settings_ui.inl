// Native dialog controls retain keyboard navigation and accessibility; only their presentation is customized.
constexpr UINT_PTR SettingsMotionTimer=72;
constexpr COLORREF AccentPresets[]{RGB(0,103,192),RGB(105,70,190),RGB(194,57,135),RGB(196,43,52),
    RGB(202,84,0),RGB(20,128,70),RGB(0,126,138),RGB(226,172,0)};
struct SettingsControl {HWND window{};RECT bounds{};int page{-1},font{};bool muted{},card{};};
struct DialogState {
    Settings& settings;HotkeyBindings& bindings;Settings draft;
    Palette palette;std::vector<SettingsControl> controls;std::array<UniqueFont,4> fonts;
    std::array<COLORREF,16> customColors{};
    float scale{1};int page{};Transition indicator;
    DialogState(Settings& source,HotkeyBindings& keys):settings(source),bindings(keys),draft(source),palette(ReadPalette(source)) {
        indicator.Snap(0);customColors.fill(RGB(255,255,255));
    }
};
int SettingsPx(float value,const DialogState& state){return static_cast<int>(std::lround(value*state.scale));}
RECT SettingsRect(RECT rect,const DialogState& state) {
    return {SettingsPx(static_cast<float>(rect.left),state),SettingsPx(static_cast<float>(rect.top),state),
        SettingsPx(static_cast<float>(rect.right),state),SettingsPx(static_cast<float>(rect.bottom),state)};
}
void SettingsText(HDC dc,const DialogState& state,const wchar_t* text,RECT rect,int font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    const auto old=SelectObject(dc,state.fonts[font].get());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,text,-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,old);
}
void LayoutSettings(HWND window,DialogState& state,bool center) {
    RECT previous{};GetWindowRect(window,&previous);MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    const UINT dpi=GetDpiForWindow(window);
    state.scale=std::min({dpi/96.f,(monitor.rcWork.right-monitor.rcWork.left-48)/680.f,(monitor.rcWork.bottom-monitor.rcWork.top-80)/552.f});
    state.scale=std::max(.5f,state.scale);
    std::array<UniqueFont,4> fonts;constexpr int sizes[]{13,12,24,16};
    for(int i=0;i<4;++i)fonts[i].reset(CreateFontW(-SettingsPx(static_cast<float>(sizes[i]),state),0,0,0,i>=2?FW_SEMIBOLD:FW_NORMAL,
        FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"));
    for(const auto& control:state.controls)SendMessageW(control.window,WM_SETFONT,reinterpret_cast<WPARAM>(fonts[control.font].get()),FALSE);
    state.fonts=std::move(fonts);
    RECT outer{0,0,SettingsPx(680,state),SettingsPx(552,state)};
    AdjustWindowRectExForDpi(&outer,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi);
    const int width=outer.right-outer.left,height=outer.bottom-outer.top;
    const int x=center?monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2:previous.left;
    const int y=center?monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2:previous.top;
    SetWindowPos(window,nullptr,std::clamp(x,static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width)),
        std::clamp(y,static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height)),width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    for(const auto& control:state.controls) {
        const auto rect=SettingsRect(control.bounds,state);
        MoveWindow(control.window,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,FALSE);
        ShowWindow(control.window,control.page<0 || control.page==state.page?SW_SHOWNOACTIVATE:SW_HIDE);
    }
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void UpdateSettingsTheme(HWND window,DialogState& state) {
    state.palette=ReadPalette(state.draft);ThemeWindow(window,state.palette,true);
    for(const auto& control:state.controls) {
        wchar_t type[24]{};GetClassNameW(control.window,type,24);
        if(wcscmp(type,L"Button")==0 || wcscmp(type,L"BUTTON")==0)InstallButtonMotion(control.window,state.draft.animations);
    }
    if(!AnimationsAllowed(state.draft)){state.indicator.Snap(static_cast<float>(state.page));KillTimer(window,SettingsMotionTimer);}
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void SelectSettingsPage(HWND window,DialogState& state,int page) {
    if(page==state.page)return;
    state.page=page;state.indicator.Start(static_cast<float>(page),AnimationsAllowed(state.draft));
    if(state.indicator.running && !SetTimer(window,SettingsMotionTimer,15,nullptr))state.indicator.Snap(static_cast<float>(page));
    for(const auto& control:state.controls)if(control.page>=0)ShowWindow(control.window,control.page==page?SW_SHOWNOACTIVATE:SW_HIDE);
    SetFocus(GetDlgItem(window,IDC_SETTINGS_APPEARANCE+page));
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void FillDialog(HWND window,DialogState& state,const Settings& settings) {
    state.draft=settings;
    SendDlgItemMessageW(window,IDC_CAPTURE_HOTKEY,HKM_SETHOTKEY,HotkeyWord(settings.capture),0);
    SendDlgItemMessageW(window,IDC_PIN_HOTKEY,HKM_SETHOTKEY,HotkeyWord(settings.pin),0);
    CheckDlgButton(window,IDC_WINDOW_SNAP,settings.windowSnap?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(window,IDC_STARTUP,settings.startup?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(window,IDC_SYSTEM_ACCENT,settings.systemAccent?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(window,IDC_ANIMATIONS,settings.animations?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(window,IDC_CHECK_UPDATES,settings.checkUpdates?BST_CHECKED:BST_UNCHECKED);
    SetDlgItemTextW(window,IDC_SETTINGS_ERROR,L"");UpdateSettingsTheme(window,state);
}
void CreateSettingsControls(HWND window,DialogState& state) {
    const auto instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));int nextLabel=2000;
    auto control=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style,RECT bounds,int page,int font=0,bool muted=false,bool card=true) {
        HWND child=CreateWindowExW(0,cls,text,WS_CHILD|style,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
            window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        if(!child)throw std::bad_alloc();
        state.controls.push_back({child,bounds,page,font,muted,card});
        if(wcscmp(cls,L"BUTTON")==0)InstallButtonMotion(child,state.draft.animations);
        return child;
    };
    auto label=[&](const wchar_t* text,RECT bounds,int page,int font=0,bool muted=false,bool card=true) {
        control(L"STATIC",text,nextLabel++,SS_LEFT,bounds,page,font,muted,card);
    };
    auto button=[&](const wchar_t* text,int id,RECT bounds,int page=-1){return control(L"BUTTON",text,id,WS_TABSTOP|BS_OWNERDRAW,bounds,page);};
    auto check=[&](const wchar_t* text,int id,RECT bounds,int page){control(L"BUTTON",text,id,WS_TABSTOP|BS_AUTOCHECKBOX,bounds,page);};
    label(L"设置",{24,20,500,56},-1,2,false,false);
    button(L"外观",IDC_SETTINGS_APPEARANCE,{24,66,112,98});
    button(L"常规",IDC_SETTINGS_GENERAL,{124,66,212,98});
    button(L"快捷键",IDC_SETTINGS_HOTKEYS,{224,66,312,98});
    label(L"主题色",{44,130,400,154},0,3);
    label(L"让按钮、选区边框和工具面板使用你喜欢的颜色。",{44,160,634,180},0,1,true);
    check(L"使用 Windows 系统强调色",IDC_SYSTEM_ACCENT,{44,188,390,218},0);
    button(L"自定义颜色…",IDC_CUSTOM_ACCENT,{484,188,634,220},0);
    constexpr const wchar_t* names[]{L"蓝色",L"紫色",L"粉色",L"红色",L"橙色",L"绿色",L"青色",L"黄色"};
    for(int i=0;i<8;++i)button(names[i],IDC_ACCENT_FIRST+i,{44+i*42,234,76+i*42,266},0);
    check(L"界面动效",IDC_ANIMATIONS,{44,372,600,400},0);
    label(L"轻量过渡；Windows 关闭动画时自动停用。",{44,400,634,420},0,1,true);
    label(L"截图",{44,130,500,154},1,3);
    check(L"自动吸附窗口",IDC_WINDOW_SNAP,{44,166,620,196},1);
    label(L"框选时按住 Shift，可以暂时跳过窗口吸附。",{44,202,634,222},1,1,true);
    label(L"启动与后台",{44,270,500,294},1,3);
    check(L"登录 Windows 后启动 Imago",IDC_STARTUP,{44,306,620,336},1);
    label(L"驻留托盘；移动程序后请重新保存开机自启。",{44,338,634,360},1,1,true);
    check(L"启动时检查更新",IDC_CHECK_UPDATES,{44,370,620,398},1);
    label(L"每天最多自动检查一次 GitHub；不下载、不自动安装。",{44,400,634,420},1,1,true);
    label(L"全局快捷键",{44,130,600,154},2,3);
    label(L"框选截图",{44,181,232,205},2);
    label(L"剪贴板贴图",{44,237,232,261},2);
    control(HOTKEY_CLASSW,L"",IDC_CAPTURE_HOTKEY,WS_TABSTOP|WS_BORDER,{260,174,634,210},2);
    control(HOTKEY_CLASSW,L"",IDC_PIN_HOTKEY,WS_TABSTOP|WS_BORDER,{260,230,634,266},2);
    button(L"截图使用 Print Screen",IDC_PRINT_SCREEN,{260,284,480,318},2);
    label(L"点击输入框后按下组合键，即可更改。",{44,328,634,350},2,1,true);
    label(L"支持 Ctrl / Alt 组合、F1–F11 或 Print Screen。",{44,356,634,378},2,1,true);
    label(L"快捷键冲突时会提示，原有绑定会保留。",{44,386,634,408},2,1,true);
    control(L"STATIC",L"",IDC_SETTINGS_ERROR,SS_LEFT,{24,440,656,474},-1,1,true,false);
    button(L"恢复默认",IDC_DEFAULTS,{24,496,136,532});
    button(L"保存",IDOK,{452,496,548,532});button(L"取消",IDCANCEL,{560,496,656,532});
    SendDlgItemMessageW(window,IDC_CAPTURE_HOTKEY,HKM_SETRULES,0,0);
    SendDlgItemMessageW(window,IDC_PIN_HOTKEY,HKM_SETRULES,0,0);
    SendMessageW(window,DM_SETDEFID,IDOK,0);
}
void PaintSettings(HWND window,DialogState& state) {
    PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);const auto& p=state.palette;
    SetDCBrushColor(dc,p.background);FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    // Derive card bottoms from their actual child bounds, retaining padding after the last label.
    // The same controls determine their background and container, so helper text cannot fall outside a card.
    auto card=[&](int top,int nextTop,int minimumBottom) {
        RECT bounds{24,top,656,minimumBottom};
        for(const auto& control:state.controls)if(control.page==state.page && control.card &&
            control.bounds.top>=top && control.bounds.top<nextTop)bounds.bottom=std::max(bounds.bottom,control.bounds.bottom+16);
        RoundFill(dc,SettingsRect(bounds,state),SettingsPx(8,state),p.surface,p.border);
    };
    if(state.page==0){card(116,358,346);card(358,440,434);}
    else if(state.page==1){card(116,256,238);card(256,440,382);}
    else card(116,440,392);
    RECT underline=SettingsRect({24,99,112,102},state);OffsetRect(&underline,SettingsPx(100*state.indicator.value,state),0);
    RoundFill(dc,underline,SettingsPx(2,state),p.accentInk,p.accentInk);
    RECT divider=SettingsRect({24,482,656,483},state);SetDCBrushColor(dc,p.border);FillRect(dc,&divider,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if(state.page==0) {
        RECT preview=SettingsRect({44,286,634,330},state);
        RoundFill(dc,preview,SettingsPx(6,state),p.background,p.border);
        RECT outline=SettingsRect({58,298,92,318},state);RoundFill(dc,outline,SettingsPx(3,state),p.surface,p.accentInk);
        wchar_t text[100]{};swprintf_s(text,L"%s · #%02X%02X%02X",state.draft.systemAccent?L"跟随系统":L"自定义主题色",GetRValue(p.accent),GetGValue(p.accent),GetBValue(p.accent));
        SettingsText(dc,state,text,SettingsRect({108,286,520,330},state),1,p.muted);
        RECT sample=SettingsRect({536,294,622,322},state);RoundFill(dc,sample,SettingsPx(4,state),p.accent,p.accent);
        SettingsText(dc,state,L"完成",sample,0,p.accentText,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    EndPaint(window,&paint);
}
void PaintSettingsButton(const DRAWITEMSTRUCT& item,DialogState& state) {
    const auto& p=state.palette;HDC dc=item.hDC;ScopedDcState saved(dc);RECT rect=item.rcItem;
    const int id=static_cast<int>(item.CtlID);const bool nav=id>=IDC_SETTINGS_APPEARANCE && id<=IDC_SETTINGS_HOTKEYS;
    const bool swatch=id>=IDC_ACCENT_FIRST && id<IDC_ACCENT_FIRST+8;
    const bool primary=id==IDOK,selected=nav && id==IDC_SETTINGS_APPEARANCE+state.page;
    const bool card=!nav && id!=IDOK && id!=IDCANCEL && id!=IDC_DEFAULTS;
    const bool enabled=(item.itemState&ODS_DISABLED)==0;
    const float hot=(item.itemState&ODS_SELECTED)?1.f:ButtonHover(item.hwndItem);
    const auto background=card?p.surface:p.background;
    SetDCBrushColor(dc,background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if(swatch) {
        const auto color=AccentPresets[id-IDC_ACCENT_FIRST];const bool active=p.accent==color;
        if(active || hot>.01f)RoundFill(dc,rect,SettingsPx(6,state),BlendColor(background,p.border,hot),active?p.accentInk:p.border);
        InflateRect(&rect,-SettingsPx(4,state),-SettingsPx(4,state));RoundFill(dc,rect,SettingsPx(4,state),color,p.border);
    }else {
        auto colors=ActionButtonColors(p,primary,enabled,hot);
        if(nav) {
            colors.fill=BlendColor(p.background,p.border,hot);colors.border=colors.fill;
            if(enabled && selected)colors.text=p.accentInk;
        }
        RoundFill(dc,rect,SettingsPx(5,state),colors.fill,colors.border);
        wchar_t text[100]{};GetWindowTextW(item.hwndItem,text,100);
        SettingsText(dc,state,text,rect,0,colors.text,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    }
    if(ButtonFocusVisible(item))DrawFocusRing(dc,item.rcItem,state.scale,primary?p.accentText:p.accentInk);
}
bool PaintSettingsCheck(NMCUSTOMDRAW& draw,DialogState& state) {
    const int id=static_cast<int>(draw.hdr.idFrom);
    if(id!=IDC_SYSTEM_ACCENT && id!=IDC_ANIMATIONS && id!=IDC_WINDOW_SNAP && id!=IDC_STARTUP && id!=IDC_CHECK_UPDATES)return false;
    if(draw.dwDrawStage!=CDDS_PREPAINT)return false;
    const auto& p=state.palette;HDC dc=draw.hdc;const int saved=SaveDC(dc);RECT rect=draw.rc;
    SetDCBrushColor(dc,p.surface);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const bool checked=SendMessageW(draw.hdr.hwndFrom,BM_GETCHECK,0,0)==BST_CHECKED;
    const int size=SettingsPx(18,state),top=(rect.bottom-size)/2;
    RECT box{rect.left,top,rect.left+size,top+size};
    RoundFill(dc,box,SettingsPx(4,state),checked?p.accent:BlendColor(p.surface,p.border,ButtonHover(draw.hdr.hwndFrom)),checked?p.accent:p.muted);
    if(checked) {
        UniquePen pen(CreatePen(PS_SOLID,std::max(1,SettingsPx(2,state)),p.accentText));ScopedSelection selectedPen(dc,pen.get());
        MoveToEx(dc,box.left+SettingsPx(4,state),box.top+SettingsPx(9,state),nullptr);
        LineTo(dc,box.left+SettingsPx(8,state),box.top+SettingsPx(13,state));LineTo(dc,box.left+SettingsPx(14,state),box.top+SettingsPx(5,state));
    }
    rect.left+=SettingsPx(28,state);wchar_t label[150]{};GetWindowTextW(draw.hdr.hwndFrom,label,150);
    SettingsText(dc,state,label,rect,0,p.text);
    if((draw.uItemState&CDIS_FOCUS) && !(SendMessageW(draw.hdr.hwndFrom,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS))DrawFocusRing(dc,rect,state.scale,p.accentInk,1);
    RestoreDC(dc,saved);return true;
}
INT_PTR CALLBACK SettingsProc(HWND window,UINT message,WPARAM w,LPARAM l) noexcept {
    try {
        auto* state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG) {
            activeSettings=window;state=reinterpret_cast<DialogState*>(l);SetWindowLongPtrW(window,DWLP_USER,l);
            CreateSettingsControls(window,*state);LayoutSettings(window,*state,true);FillDialog(window,*state,state->settings);
            SetFocus(GetDlgItem(window,IDC_SETTINGS_APPEARANCE));return FALSE;
        }
        if(!state)return FALSE;
        switch(message) {
        case WM_ERASEBKGND:return TRUE;
        case WM_PAINT:PaintSettings(window,*state);return TRUE;
        case WM_DRAWITEM:PaintSettingsButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l),*state);return TRUE;
        case WM_NOTIFY:
            if(reinterpret_cast<NMHDR*>(l)->code==NM_CUSTOMDRAW && PaintSettingsCheck(*reinterpret_cast<NMCUSTOMDRAW*>(l),*state)) {
                SetWindowLongPtrW(window,DWLP_MSGRESULT,CDRF_SKIPDEFAULT);return TRUE;
            }break;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT: {
            const auto child=reinterpret_cast<HWND>(l);const auto& p=state->palette;
            const auto found=std::find_if(state->controls.begin(),state->controls.end(),[&](const auto& item){return item.window==child;});
            const auto background=found!=state->controls.end() && found->card?p.surface:p.background;
            HDC dc=reinterpret_cast<HDC>(w);SetBkColor(dc,background);SetDCBrushColor(dc,background);
            SetTextColor(dc,GetDlgCtrlID(child)==IDC_SETTINGS_ERROR?(p.highContrast?p.text:p.dark?RGB(255,150,150):RGB(183,30,36)):
                found!=state->controls.end() && found->muted?p.muted:p.text);
            return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
        }
        case WM_DPICHANGED:LayoutSettings(window,*state,false);return TRUE;
        case WM_SETTINGCHANGE:case WM_THEMECHANGED:case WM_DWMCOLORIZATIONCOLORCHANGED:UpdateSettingsTheme(window,*state);return TRUE;
        case WM_TIMER:
            if(w==SettingsMotionTimer) {
                if(!AnimationsAllowed(state->draft))state->indicator.Snap(static_cast<float>(state->page));else state->indicator.Tick();
                if(!state->indicator.running)KillTimer(window,SettingsMotionTimer);
                InvalidateRect(window,nullptr,FALSE);return TRUE;
            }break;
        case WM_COMMAND: {
            const int id=LOWORD(w);if(HIWORD(w)!=BN_CLICKED && id!=IDOK && id!=IDCANCEL)break;
            if(id>=IDC_SETTINGS_APPEARANCE && id<=IDC_SETTINGS_HOTKEYS){SelectSettingsPage(window,*state,id-IDC_SETTINGS_APPEARANCE);return TRUE;}
            if(id>=IDC_ACCENT_FIRST && id<IDC_ACCENT_FIRST+8) {
                state->draft.accentColor=AccentPresets[id-IDC_ACCENT_FIRST];state->draft.systemAccent=false;
                CheckDlgButton(window,IDC_SYSTEM_ACCENT,BST_UNCHECKED);UpdateSettingsTheme(window,*state);return TRUE;
            }
            switch(id) {
            case IDC_SYSTEM_ACCENT:state->draft.systemAccent=IsDlgButtonChecked(window,id)==BST_CHECKED;UpdateSettingsTheme(window,*state);return TRUE;
            case IDC_ANIMATIONS:state->draft.animations=IsDlgButtonChecked(window,id)==BST_CHECKED;UpdateSettingsTheme(window,*state);return TRUE;
            case IDC_CUSTOM_ACCENT: {
                CHOOSECOLORW dialog{sizeof(dialog)};dialog.hwndOwner=window;dialog.rgbResult=state->palette.accent;
                dialog.lpCustColors=state->customColors.data();dialog.Flags=CC_FULLOPEN|CC_RGBINIT|CC_ANYCOLOR;
                if(ChooseColorW(&dialog)) {
                    state->draft.accentColor=dialog.rgbResult;state->draft.systemAccent=false;CheckDlgButton(window,IDC_SYSTEM_ACCENT,BST_UNCHECKED);UpdateSettingsTheme(window,*state);
                }else if(CommDlgExtendedError())SetDlgItemTextW(window,IDC_SETTINGS_ERROR,L"无法打开系统选色器，请重试。");
                return TRUE;
            }
            case IDC_PRINT_SCREEN:
                SendDlgItemMessageW(window,IDC_CAPTURE_HOTKEY,HKM_SETHOTKEY,HotkeyWord({0,VK_SNAPSHOT}),0);
                SetDlgItemTextW(window,IDC_SETTINGS_ERROR,L"如果 Print Screen 被 Windows 截图工具占用，请先关闭系统的按键映射。");return TRUE;
            case IDC_DEFAULTS:FillDialog(window,*state,Settings{});return TRUE;
            case IDCANCEL:EndDialog(window,IDCANCEL);return TRUE;
            case IDOK: {
                Settings next=state->draft;next.capture=ReadHotkey(window,IDC_CAPTURE_HOTKEY);next.pin=ReadHotkey(window,IDC_PIN_HOTKEY);
                next.windowSnap=IsDlgButtonChecked(window,IDC_WINDOW_SNAP)==BST_CHECKED;next.startup=IsDlgButtonChecked(window,IDC_STARTUP)==BST_CHECKED;
                next.checkUpdates=IsDlgButtonChecked(window,IDC_CHECK_UPDATES)==BST_CHECKED;
                std::wstring error;
                if(!state->bindings.ApplyAndSave(next,error)) {
                    if(!Valid(next.capture) || !Valid(next.pin) || next.capture==next.pin || error.starts_with(L"截图快捷键") || error.starts_with(L"贴图快捷键"))SelectSettingsPage(window,*state,2);
                    SetDlgItemTextW(window,IDC_SETTINGS_ERROR,error.c_str());return TRUE;
                }
                state->settings=next;EndDialog(window,IDOK);return TRUE;
            }
            }break;
        }
        case WM_CLOSE:EndDialog(window,IDCANCEL);return TRUE;
        case WM_NCDESTROY:KillTimer(window,SettingsMotionTimer);if(activeSettings==window)activeSettings=nullptr;break;
        }
    }catch(...) {
        if(message==WM_INITDIALOG){EndDialog(window,-1);return TRUE;}
        SetDlgItemTextW(window,IDC_SETTINGS_ERROR,L"操作未完成：系统资源不足，请重试。");
    }
    return FALSE;
}
