// Automatic capture is a timer-driven state machine on the UI thread. No background input injection.
RECT PreviewViewport(HWND window) {
    RECT rect{};GetClientRect(window,&rect);const float s=Scale(window);
    rect.left+=Px(12,s);rect.right-=Px(12,s);rect.top=Px(88,s);rect.bottom-=Px(12,s);
    rect.right=std::max(rect.right,rect.left+1);rect.bottom=std::max(rect.bottom,rect.top+1);return rect;
}
void LayoutLongPreview(HWND window,LongPreview& preview) {
    const float s=Scale(window);RECT view=PreviewViewport(window);
    preview.scale=std::min(1.0,static_cast<double>(view.right-view.left)/preview.image.width);
    const int height=static_cast<int>(std::ceil(preview.image.height*preview.scale));
    preview.position=std::clamp(preview.position,0,std::max(0,height-static_cast<int>(view.bottom-view.top)));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=std::max(0,height-1);
    info.nPage=static_cast<UINT>(view.bottom-view.top);info.nPos=preview.position;SetScrollInfo(window,SB_VERT,&info,TRUE);
    for(std::size_t i=0;i<preview.buttons.size();++i)MoveWindow(preview.buttons[i],Px(12+36.f*static_cast<float>(i),s),Px(10,s),Px(32,s),Px(32,s),TRUE);
    InvalidateRect(window,nullptr,FALSE);
}
void ScrollLongPreview(HWND window,LongPreview& preview,int position) {
    preview.position=position;LayoutLongPreview(window,preview);
}
bool CreateLongPreview(Image& image,const std::wstring& note,bool activate) {
    if(!image.valid())return false;
    POINT cursor{};GetCursorPos(&cursor);RECT work=MonitorWork(cursor);
    const int width=std::min(980L,work.right-work.left-32),height=std::min(800L,work.bottom-work.top-32);
    auto preview=std::make_unique<LongPreview>();preview->note=note;
    const auto title=L"Imago — 长截图 "+std::to_wstring(image.width)+L" × "+std::to_wstring(image.height);
    HWND window=CreateWindowExW(0,LongPreviewClass,title.c_str(),WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_CLIPCHILDREN,
        work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,
        nullptr,nullptr,app.instance,nullptr);
    if(!window)return false;
    preview->image=std::move(image);
    auto& state=*preview;
    try {app.longPreviews.emplace(window,std::move(preview));}catch(...){DestroyWindow(window);throw;}
    ThemeWindow(window,app.palette,true);state.font.reset(MakeFont(12,Scale(window)));
    state.tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,app.instance,nullptr);
    const std::pair<UINT,const wchar_t*> actions[]{
        {CopyCommand,L"复制长图 (Ctrl+C)"},{SaveCommand,L"保存 PNG (Ctrl+S)"},
        {OcrCommand,L"识别文字并复制 (Ctrl+Shift+C)"},{PinCommand,L"贴图 (P)"},{CancelCommand,L"关闭 (Esc)"}};
    for(const auto& [id,text]:actions) {
        HWND button=Button(window,text,id,state.font.get());state.buttons.push_back(button);
        if(state.tooltip && button) {
            TOOLINFOW info{sizeof(info)};info.hwnd=window;info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
            info.uId=reinterpret_cast<UINT_PTR>(button);info.lpszText=const_cast<wchar_t*>(text);
            SendMessageW(state.tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
        }
    }
    LayoutLongPreview(window,state);ShowWindow(window,activate?SW_SHOW:SW_SHOWNOACTIVATE);
    if(activate){SetForegroundWindow(window);SetFocus(window);}return true;
}
void RecognizeLongText(HWND window) {
    if(app.modal || app.scroll || app.overlay)return;
    auto found=app.longPreviews.find(window);if(found==app.longPreviews.end() || found->second->ocrBusy)return;
    if(StartOcrImage(found->second->image,window)){found->second->ocrBusy=true;InvalidateRect(window,nullptr,FALSE);}
}
LRESULT CALLBACK LongPreviewProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto found=app.longPreviews.find(window);if(found==app.longPreviews.end())return DefWindowProcW(window,message,w,l);
    auto& preview=*found->second;
    switch(message) {
    case WM_GETMINMAXINFO:{
        auto& limits=*reinterpret_cast<MINMAXINFO*>(l);const float s=Scale(window);
        limits.ptMinTrackSize={Px(360,s),Px(220,s)};return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:LayoutLongPreview(window,preview);return 0;
    case WM_PAINT:{
        PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT client{};GetClientRect(window,&client);
        SetDCBrushColor(dc,app.palette.background);FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const int saved=SaveDC(dc);RECT view=PreviewViewport(window);
        IntersectClipRect(dc,view.left,view.top,view.right,view.bottom);
        const int first=std::min(preview.image.height-1,static_cast<int>(preview.position/preview.scale));
        const double offset=preview.position-first*preview.scale;
        const int rows=std::min(preview.image.height-first,static_cast<int>(std::ceil((view.bottom-view.top+offset)/preview.scale))+1);
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),preview.image.width,-rows,1,32,BI_RGB,0,0,0,0,0};
        const int imageWidth=static_cast<int>(std::lround(preview.image.width*preview.scale));
        SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
        StretchDIBits(dc,view.left+(view.right-view.left-imageWidth)/2,view.top-static_cast<int>(std::lround(offset)),
            imageWidth,static_cast<int>(std::lround(rows*preview.scale)),0,0,preview.image.width,rows,
            preview.image.pixels.data()+static_cast<std::size_t>(first)*preview.image.width,&info,DIB_RGB_COLORS,SRCCOPY);
        RestoreDC(dc,saved);
        auto old=SelectObject(dc,preview.font.get());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,app.palette.text);
        const float s=Scale(window);RECT text{Px(204,s),Px(10,s),client.right-Px(12,s),Px(42,s)};
        const auto dimensions=std::to_wstring(preview.image.width)+L" × "+std::to_wstring(preview.image.height)+L" px";
        DrawTextW(dc,dimensions.c_str(),-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        RECT status{Px(12,s),Px(50,s),client.right-Px(12,s),Px(78,s)};SetTextColor(dc,app.palette.muted);
        DrawTextW(dc,preview.ocrBusy?L"正在识别文字… Esc 取消":preview.note.c_str(),-1,&status,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc,old);EndPaint(window,&ps);return 0;
    }
    case WM_DRAWITEM:{
        const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);using Icon=imago::ToolbarIcon;
        Icon icon=Icon::Cancel;
        if(item.CtlID==CopyCommand)icon=Icon::Copy;else if(item.CtlID==SaveCommand)icon=Icon::Save;
        else if(item.CtlID==OcrCommand)icon=Icon::Ocr;else if(item.CtlID==PinCommand)icon=Icon::Pin;
        imago::IconButtonStyle style;
        const bool primary=item.CtlID==CopyCommand;
        const float hover=(item.itemState&ODS_SELECTED)?1.f:imago::ButtonHover(item.hwndItem);
        const auto colors=imago::ActionButtonColors(app.palette,primary,(item.itemState&ODS_DISABLED)==0,hover);
        style.background=app.palette.background;style.fill=colors.fill;
        style.foreground=colors.text;style.border=colors.border;
        style.focus=colors.focus;style.focused=imago::ButtonFocusVisible(item);style.pressed=(item.itemState&ODS_SELECTED)!=0;
        if(!preview.renderer.Draw(item.hDC,item.rcItem,icon,style,app.factory.Get()))PaintButton(item);return TRUE;
    }
    case WM_VSCROLL:{
        SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,SB_VERT,&info);int next=preview.position;
        switch(LOWORD(w)) {
        case SB_LINEUP:next-=40;break;case SB_LINEDOWN:next+=40;break;
        case SB_PAGEUP:next-=static_cast<int>(info.nPage);break;case SB_PAGEDOWN:next+=static_cast<int>(info.nPage);break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;
        case SB_TOP:next=0;break;case SB_BOTTOM:next=info.nMax;break;
        }
        ScrollLongPreview(window,preview,next);return 0;
    }
    case WM_MOUSEWHEEL:ScrollLongPreview(window,preview,preview.position-GET_WHEEL_DELTA_WPARAM(w)*120/WHEEL_DELTA);return 0;
    case WM_KEYDOWN:
        if(w==VK_ESCAPE){if(preview.ocrBusy)CancelOcr();else DestroyWindow(window);return 0;}
        if(w=='C' && (GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(window,WM_COMMAND,(GetKeyState(VK_SHIFT)&0x8000)?OcrCommand:CopyCommand,0);return 0;}
        if(w=='S' && (GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(window,WM_COMMAND,SaveCommand,0);return 0;}
        if(w=='P'){SendMessageW(window,WM_COMMAND,PinCommand,0);return 0;}
        if(w==VK_UP || w==VK_DOWN || w==VK_PRIOR || w==VK_NEXT || w==VK_HOME || w==VK_END) {
            const WPARAM code=w==VK_UP?SB_LINEUP:w==VK_DOWN?SB_LINEDOWN:w==VK_PRIOR?SB_PAGEUP:w==VK_NEXT?SB_PAGEDOWN:w==VK_HOME?SB_TOP:SB_BOTTOM;
            SendMessageW(window,WM_VSCROLL,code,0);return 0;
        }break;
    case WM_COMMAND:
        if(app.modal)return 0;
        if(LOWORD(w)==OcrCommand){RecognizeLongText(window);return 0;}
        if(LOWORD(w)==CancelCommand){DestroyWindow(window);return 0;}
        if(preview.ocrBusy)CancelOcr(false);
        if(LOWORD(w)==CopyCommand){std::wstring error;if(!imago::CopyImage(app.controller,preview.image,error))Notify(error.c_str(),true);else Notify(L"长截图已复制。");}
        else if(LOWORD(w)==SaveCommand) {
            CaptureModalScope modal;Image image=preview.image;std::wstring error;
            if(imago::SavePngDialog(window,image,error)==imago::SaveResult::Failed)Notify(error.c_str(),true);
        }else if(LOWORD(w)==PinCommand){POINT point{};GetCursorPos(&point);if(!CreatePin(preview.image,point))Notify(L"无法创建长图贴图。",true);}
        return 0;
    case WM_DPICHANGED:{
        const RECT rect=*reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
        imago::UniqueFont font(MakeFont(12,Scale(window)));
        for(HWND button:preview.buttons)SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font.get()),FALSE);
        preview.font=std::move(font);LayoutLongPreview(window,preview);return 0;
    }
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_NCDESTROY:
        if(app.ocrSource==window)CancelOcr(false);app.longPreviews.erase(found);return DefWindowProcW(window,message,w,l);
    }
    return DefWindowProcW(window,message,w,l);
}
bool LongPreviewKey(const MSG& message) {
    if(message.message!=WM_KEYDOWN || app.modal)return false;
    const HWND root=GetAncestor(message.hwnd,GA_ROOT);
    if(!app.longPreviews.contains(root))return false;
    const auto key=message.wParam;const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    if(key==VK_RETURN) {
        const UINT id=message.hwnd==root?CopyCommand:static_cast<UINT>(GetDlgCtrlID(message.hwnd));
        SendMessageW(root,WM_COMMAND,id,0);return true;
    }
    if(key==VK_ESCAPE || (control && (key=='C' || key=='S')) || (!control && key=='P') ||
       key==VK_UP || key==VK_DOWN || key==VK_PRIOR || key==VK_NEXT || key==VK_HOME || key==VK_END) {
        SendMessageW(root,WM_KEYDOWN,key,message.lParam);return true;
    }
    return false;
}
void RefreshScrollPreview(ScrollSession& session,bool imageChanged=false) {
    if(imageChanged)session.thumbnail=imago::Thumbnail(session.capture.Stitch().Result(),1000);
    if(session.progress)RedrawWindow(session.progress,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
constexpr ULONG ScrollInputTag=0x494D4753; // Identify our own injected wheel events, including driver echoes.
void PauseScrollCapture(ScrollSession& session,imago::ScrollPause reason) {
    session.resumeRequested=false;
    session.capture.Pause(reason,GetTickCount64());
    RefreshScrollPreview(session);
}
const wchar_t* ScrollStatus(const ScrollSession& session) {
    switch(session.capture.PauseReason()) {
    case imago::ScrollPause::NoMovement:return L"画面未移动，可反向或手动继续";
    case imago::ScrollPause::Unstable:return L"画面仍在变化，已切为手动，可反向或完成";
    case imago::ScrollPause::Unaligned:return L"暂时无法对齐，已切为手动，可反向少量滚动";
    case imago::ScrollPause::TargetFocus:return L"请返回目标窗口，再选择采集方向";
    case imago::ScrollPause::CursorPosition:return L"无法定位滚动区域，可移入选区手动滚动";
    case imago::ScrollPause::None:break;
    }
    if(session.inputHeld)return L"松开按键后继续采集";
    if(session.capture.Automatic() && session.cursorOutside)return L"鼠标移回选区即可继续";
    if(!session.escapeRegistered)return L"点击完成保留 · 取消返回选区";
    return session.capture.Automatic()?L"滚轮可接管 · Esc 完成":L"在选区内上下滚动 · Esc 完成";
}
void ScrollRawInput(HRAWINPUT handle) {
    if(!app.scroll)return;
    RAWINPUT input{};UINT bytes=sizeof(input);
    if(GetRawInputData(handle,RID_INPUT,&input,&bytes,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1) ||
       input.header.dwType!=RIM_TYPEMOUSE || !(input.data.mouse.usButtonFlags&RI_MOUSE_WHEEL) ||
       input.data.mouse.ulExtraInformation==ScrollInputTag)return;
    auto& session=*app.scroll;POINT cursor{};GetCursorPos(&cursor);
    if(!PtInRect(&session.region,cursor) || GetAncestor(WindowFromPoint(cursor),GA_ROOT)!=session.target ||
       GetForegroundWindow()!=session.target)return;
    // Observe real wheel input without consuming it. The target application still receives every event.
    session.resumeRequested=false;
    session.capture.ManualWheel(static_cast<SHORT>(input.data.mouse.usButtonData),GetTickCount64());
    RefreshScrollPreview(session);
}
void SetScrollMode(imago::ScrollMode mode) {
    if(!app.scroll)return;
    auto& session=*app.scroll;
    // BN_CLICKED is delivered inside the button's mouse-up handling. Rearm on the next timer tick,
    // after capture/focus have been released, so the panel cannot receive the first reversed wheel.
    session.capture.SelectMode(mode,GetTickCount64());
    session.resumeRequested=session.capture.Automatic();
    RefreshScrollPreview(session);
}
RECT ScrollPreviewArea(HWND window) {
    RECT rect{};GetClientRect(window,&rect);const float s=Scale(window);
    rect.left+=Px(12,s);rect.right-=Px(12,s);rect.top=Px(100,s);rect.bottom-=Px(53,s);
    rect.right=std::max(rect.left+1,rect.right);rect.bottom=std::max(rect.top+1,rect.bottom);return rect;
}
void LayoutScrollPanel(HWND window) {
    if(!app.scroll)return;
    const float s=Scale(window);RECT rect{};GetClientRect(window,&rect);
    const int gap=Px(6,s),margin=Px(12,s),available=rect.right-2*margin;
    const int third=std::max(1,(available-2*gap)/3),half=std::max(1,(available-gap)/2);
    const UINT modes[]{ScrollUpCommand,ScrollDownCommand,ScrollManualCommand};
    for(int i=0;i<3;++i)MoveWindow(GetDlgItem(window,modes[i]),margin+i*(third+gap),Px(61,s),third,Px(29,s),TRUE);
    MoveWindow(GetDlgItem(window,FinishScrollCommand),margin,rect.bottom-Px(41,s),half,Px(29,s),TRUE);
    MoveWindow(GetDlgItem(window,DiscardScrollCommand),margin+half+gap,rect.bottom-Px(41,s),half,Px(29,s),TRUE);
    InvalidateRect(window,nullptr,FALSE);
}
RECT PlaceScrollPanel(const ScrollSession& session,float scale) {
    const RECT work=MonitorWork(session.anchor);const int gap=Px(12,scale);
    const int width=std::min(Px(280,scale),static_cast<int>(work.right-work.left)-2*gap);
    const int height=std::min(Px(520,scale),static_cast<int>(work.bottom-work.top)-2*gap);
    // Prefer the right-hand side, then the left, without covering any selected pixels.
    RECT right{session.region.right+gap,session.region.top,session.region.right+gap+width,session.region.top+height};
    RECT left{session.region.left-gap-width,session.region.top,session.region.left-gap,session.region.top+height};
    for(RECT candidate:{right,left}) {
        if(candidate.left<work.left+gap || candidate.right>work.right-gap)continue;
        ClampWindowToWork(candidate,work);return candidate;
    }
    struct MonitorSearch {RECT region;int width,height,gap;RECT chosen{};};
    MonitorSearch search{session.region,width,height,gap};
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM context)->BOOL {
        auto& item=*reinterpret_cast<MonitorSearch*>(context);MONITORINFO info{sizeof(info)};
        if(!GetMonitorInfoW(monitor,&info))return TRUE;
        RECT overlap{};if(IntersectRect(&overlap,&info.rcMonitor,&item.region))return TRUE;
        if(info.rcWork.right-info.rcWork.left<item.width+2*item.gap || info.rcWork.bottom-info.rcWork.top<item.height+2*item.gap)return TRUE;
        item.chosen={info.rcWork.left+item.gap,info.rcWork.top+item.gap,info.rcWork.left+item.gap+item.width,info.rcWork.top+item.gap+item.height};return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    if(search.chosen.right>search.chosen.left)return search.chosen;
    const int compactHeight=std::min(height,Px(300,scale));
    for(RECT candidate:{RECT{session.region.left,session.region.bottom+gap,session.region.left+width,session.region.bottom+gap+compactHeight},
                        RECT{session.region.left,session.region.top-gap-compactHeight,session.region.left+width,session.region.top-gap}}) {
        if(candidate.top<work.top+gap || candidate.bottom>work.bottom-gap)continue;
        ClampWindowToWork(candidate,work);return candidate;
    }
    // Full-screen selections have no exterior space. Hide this compact panel while acquiring each frame.
    RECT fallback{work.right-gap-width,work.top+gap,work.right-gap,work.top+gap+compactHeight};
    if(PtInRect(&fallback,session.anchor))fallback={work.left+gap,work.bottom-gap-compactHeight,work.left+gap+width,work.bottom-gap};
    return fallback;
}
void PaintScrollPreview(HDC dc,HWND window,const ScrollSession& session) {
    RECT view=ScrollPreviewArea(window);SetDCBrushColor(dc,app.palette.surface);
    FillRect(dc,&view,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const auto& image=session.thumbnail;
    if(!image.valid()) {
        SetTextColor(dc,app.palette.muted);DrawTextW(dc,L"正在准备预览…",-1,&view,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;
    }
    const double scale=std::min(static_cast<double>(view.right-view.left)/image.width,static_cast<double>(view.bottom-view.top)/image.height);
    const int width=std::max(1,static_cast<int>(std::lround(image.width*scale))),height=std::max(1,static_cast<int>(std::lround(image.height*scale)));
    const int x=view.left+(view.right-view.left-width)/2,y=view.top+(view.bottom-view.top-height)/2;
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),image.width,-image.height,1,32,BI_RGB,0,0,0,0,0};
    SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
    StretchDIBits(dc,x,y,width,height,0,0,image.width,image.height,image.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    const auto& stitch=session.capture.Stitch();
    const int top=y+static_cast<int>(static_cast<double>(stitch.Position())/stitch.Height()*height);
    const int bottom=y+static_cast<int>(static_cast<double>(stitch.Position()+stitch.ViewportHeight())/stitch.Height()*height);
    HPEN pen=CreatePen(PS_SOLID,std::max(1,Px(2,Scale(window))),app.palette.accent);
    auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc,x,top,x+width,std::max(top+2,bottom));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
}
bool ScrollTargetValid(const ScrollSession& session,bool allowProgress) {
    if(!IsWindow(session.target) || !IsWindowVisible(session.target) || IsIconic(session.target))return false;
    DWORD process{};const DWORD thread=GetWindowThreadProcessId(session.target,&process);
    if(process!=session.process || thread!=session.thread)return false;
    RECT bounds{};GetWindowRect(session.target,&bounds);if(!EqualRect(&bounds,&session.targetBounds))return false;
    const HWND foreground=GetAncestor(GetForegroundWindow(),GA_ROOT);
    if(foreground!=session.target && foreground!=session.progress)return false;
    for(int y:{session.region.top+4,(session.region.top+session.region.bottom)/2,session.region.bottom-5})
        for(int x:{session.region.left+4,(session.region.left+session.region.right)/2,session.region.right-5}) {
            HWND hit=GetAncestor(WindowFromPoint({x,y}),GA_ROOT);
            if(hit!=session.target && !(allowProgress && hit==session.progress))return false;
        }
    return true;
}
Image ReadScrollFrame(ScrollSession& session,std::wstring& error) {
    RECT progress{},overlap{};GetWindowRect(session.progress,&progress);
    const bool hide=IntersectRect(&overlap,&progress,&session.region)!=FALSE;
    if(hide)ShowWindow(session.progress,SW_HIDE);
    struct RestoreProgress { HWND window;bool restore;~RestoreProgress(){if(restore && IsWindow(window))ShowWindow(window,SW_SHOWNOACTIVATE);} } restore{session.progress,hide};
    DwmFlush();
    if(!ScrollTargetValid(session,false)){error=L"窗口位置或前台应用已变化，停止滚动。";return {};}
    return imago::CaptureRegion(session.region,error);
}
void EndScrollCapture(bool discard,const wchar_t* reason,bool shuttingDown) {
    if(!app.scroll)return;
    KillTimer(app.controller,ScrollTimer);
    auto session=std::move(app.scroll);
    if(session->escapeRegistered)UnregisterHotKey(app.controller,ScrollStopHotkey);
    if(session->rawInputRegistered){RAWINPUTDEVICE device{1,2,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&device,1,sizeof(device));}
    const bool activate=GetForegroundWindow()==session->target || GetForegroundWindow()==session->progress;
    if(session->progress)DestroyWindow(session->progress);
    POINT cursor{};GetCursorPos(&cursor);
    if(std::abs(cursor.x-session->anchor.x)<=8 && std::abs(cursor.y-session->anchor.y)<=8)SetCursorPos(session->previousCursor.x,session->previousCursor.y);
    if(shuttingDown)return;
    if(!discard && session->capture.Stitch().Frames()>0) {
        Image image=session->capture.TakeImage();
        const auto note=std::wstring(reason)+L"  ·  "+std::to_wstring(session->capture.Stitch().Frames())+L" 帧 · 滚轮查看，Ctrl+S 保存";
        if(CreateLongPreview(image,note,activate)){CloseCapture(false);return;}
        Notify(L"无法打开长截图预览，原选区已保留。",true);
    }else if(reason && *reason)Notify(reason);
    if(app.overlay && app.capture){ShowWindow(app.overlay,SW_SHOW);SetForegroundWindow(app.overlay);SetFocus(app.overlay);LayoutToolbar();}
}
bool ScrollModifiersHeld() {
    for(int key:{VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON})if(GetAsyncKeyState(key)&0x8000)return true;
    return false;
}
void TickScrollCapture() {
    if(!app.scroll)return;
    auto& session=*app.scroll;auto& capture=session.capture;const auto now=GetTickCount64();
    if(capture.TimeLimitReached(now)){EndScrollCapture(false,L"达到 5 分钟采集上限。");return;}
    if(session.resumeRequested) {
        const HWND captured=GetCapture();
        if(ScrollModifiersHeld() || (captured && GetAncestor(captured,GA_ROOT)==session.progress)) {
            capture.WaitForInput(now);return;
        }
        DWORD process{};const DWORD thread=GetWindowThreadProcessId(session.target,&process);
        if(!IsWindow(session.target) || process!=session.process || thread!=session.thread){EndScrollCapture(false,L"目标窗口已关闭，已保留采集内容。");return;}
        const HWND foreground=GetForegroundWindow();
        if(foreground!=session.target && foreground!=session.progress){EndScrollCapture(false,L"前台应用已变化，已保留采集内容。");return;}
        if(!SetForegroundWindow(session.target) && GetForegroundWindow()!=session.target) {
            PauseScrollCapture(session,imago::ScrollPause::TargetFocus);return;
        }
        if(!SetCursorPos(session.anchor.x,session.anchor.y)) {
            PauseScrollCapture(session,imago::ScrollPause::CursorPosition);return;
        }
        session.resumeRequested=false;capture.WaitForInput(now);
    }
    if(!ScrollTargetValid(session,true)){EndScrollCapture(false,L"目标窗口或前台应用变化，已停止。");return;}
    POINT cursor{};GetCursorPos(&cursor);
    const bool outside=!PtInRect(&session.region,cursor) || GetAncestor(WindowFromPoint(cursor),GA_ROOT)!=session.target;
    const bool held=ScrollModifiersHeld();
    if(session.cursorOutside!=outside || session.inputHeld!=held) {
        session.cursorOutside=outside;session.inputHeld=held;RefreshScrollPreview(session);
    }
    // Mouse movement inside the region is harmless. Leaving it only gates automatic input, not stitching.
    if(held){capture.WaitForInput(now);return;}
    if(capture.Phase()==imago::ScrollPhase::Wheel) {
        if(outside || !capture.WheelDue(now))return;
        if(capture.StepLimitReached()){EndScrollCapture(false,L"达到 120 次滚动上限。");return;}
        if(GetForegroundWindow()!=session.target)return;
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_WHEEL;
        input.mi.mouseData=static_cast<DWORD>(capture.WheelDelta());
        input.mi.dwExtraInfo=ScrollInputTag;
        if(SendInput(1,&input,sizeof(input))!=1){EndScrollCapture(false,L"目标程序未接受滚动输入。");return;}
        capture.WheelSent(now);return;
    }
    if(!capture.FrameDue(now))return;
    const bool initial=capture.Phase()==imago::ScrollPhase::Initial;
    std::wstring error;auto frame=ReadScrollFrame(session,error);
    if(!frame.valid()){EndScrollCapture(initial,error.c_str());return;}
    if(initial && frame.pixels.size()>imago::MaxScrollBytes/4){EndScrollCapture(true,L"选区过大，请缩小后重试。");return;}
    switch(capture.ObserveFrame(std::move(frame),now)) {
    case imago::ScrollUpdate::Limit:EndScrollCapture(false,L"达到长图尺寸或内存上限。");return;
    case imago::ScrollUpdate::Image:RefreshScrollPreview(session,true);break;
    case imago::ScrollUpdate::Status:RefreshScrollPreview(session);break;
    case imago::ScrollUpdate::None:break;
    }
}
void ScrollTick() {
    try {TickScrollCapture();}catch(...) {EndScrollCapture(false,L"系统资源不足，已停止采集。");}
}
LRESULT CALLBACK ScrollProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(!app.scroll)return DefWindowProcW(window,message,w,l);
    auto& session=*app.scroll;
    switch(message) {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:LayoutScrollPanel(window);return 0;
    case WM_DRAWITEM:{
        auto item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);
        const auto mode=session.capture.Mode();
        if(item.CtlID==(mode==imago::ScrollMode::Up?ScrollUpCommand:mode==imago::ScrollMode::Down?ScrollDownCommand:ScrollManualCommand))item.itemState|=ODS_SELECTED;
        PaintButton(item);return TRUE;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT rect{};GetClientRect(window,&rect);
        SetDCBrushColor(dc,app.palette.background);FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        const float s=Scale(window);auto old=SelectObject(dc,session.font.get());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,app.palette.text);
        RECT line{Px(12,s),Px(8,s),rect.right-Px(12,s),Px(31,s)};
        const auto mode=session.capture.Mode();const auto& stitch=session.capture.Stitch();
        const auto text=std::wstring(mode==imago::ScrollMode::Up?L"向上采集 · ":mode==imago::ScrollMode::Down?L"向下采集 · ":L"手动滚动 · ")+
            std::to_wstring(stitch.Result().width)+L" × "+std::to_wstring(stitch.Height());
        DrawTextW(dc,text.c_str(),-1,&line,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        line.top=Px(32,s);line.bottom=Px(53,s);SetTextColor(dc,app.palette.muted);
        DrawTextW(dc,ScrollStatus(session),-1,&line,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        PaintScrollPreview(dc,window,session);
        SelectObject(dc,old);EndPaint(window,&ps);return 0;
    }
    case WM_COMMAND:
        if(LOWORD(w)==FinishScrollCommand)EndScrollCapture(false);
        else if(LOWORD(w)==DiscardScrollCommand)EndScrollCapture(true,L"已取消长截图，原选区已保留。");
        else if(LOWORD(w)==ScrollUpCommand)SetScrollMode(imago::ScrollMode::Up);
        else if(LOWORD(w)==ScrollDownCommand)SetScrollMode(imago::ScrollMode::Down);
        else if(LOWORD(w)==ScrollManualCommand)SetScrollMode(imago::ScrollMode::Manual);return 0;
    case WM_DPICHANGED:{
        const RECT rect=*reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
        imago::UniqueFont font(MakeFont(12,Scale(window)));
        for(UINT id:{ScrollUpCommand,ScrollDownCommand,ScrollManualCommand,FinishScrollCommand,DiscardScrollCommand})
            SendMessageW(GetDlgItem(window,id),WM_SETFONT,reinterpret_cast<WPARAM>(font.get()),FALSE);
        session.font=std::move(font);LayoutScrollPanel(window);return 0;
    }
    case WM_CLOSE:EndScrollCapture(true,L"已取消长截图，原选区已保留。");return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
void StartScrollCapture() {
    if(app.modal || app.scroll || !app.capture)return;
    auto& capture=*app.capture;
    if(!capture.selected || capture.ocrBusy || capture.dragging || capture.pickingColor || capture.annotationDrag!=AnnotationDrag::None)return;
    EndTextEdit(true);
    if(!capture.document.items.empty()){Notify(L"请在添加标注前开始长截图，当前标注已保留。");return;}
    if(app.longPreviews.size()>=4){Notify(L"请先关闭部分长截图预览（最多 4 个）。",true);return;}
    if(capture.selection.right-capture.selection.left<120 || capture.selection.bottom-capture.selection.top<240){Notify(L"长截图选区至少需要 120 × 240 像素，请框选可滚动内容区域。");return;}
    try {
        CancelOcr(false);auto session=std::make_unique<ScrollSession>();
        session->region=capture.selection;OffsetRect(&session->region,capture.bounds.left,capture.bounds.top);
        session->anchor={(session->region.left+session->region.right)/2,(session->region.top+session->region.bottom)/2};
        GetCursorPos(&session->previousCursor);
        if(capture.tooltip)SendMessageW(capture.tooltip,TTM_POP,0,0);
        ShowWindow(app.overlay,SW_HIDE);DwmFlush();
        session->target=GetAncestor(WindowFromPoint(session->anchor),GA_ROOT);
        session->thread=GetWindowThreadProcessId(session->target,&session->process);
        GetWindowRect(session->target,&session->targetBounds);
        RECT overlap{};IntersectRect(&overlap,&session->region,&session->targetBounds);
        if(!session->target || session->process==GetCurrentProcessId() || !EqualRect(&overlap,&session->region)) {
            ShowWindow(app.overlay,SW_SHOW);SetForegroundWindow(app.overlay);Notify(L"请在同一个应用窗口内框选可滚动区域。",true);return;
        }
        app.scroll=std::move(session);auto& state=*app.scroll;
        if(!SetForegroundWindow(state.target) && GetForegroundWindow()!=state.target){EndScrollCapture(true,L"无法激活目标窗口，请重新选择。");return;}
        if(!SetCursorPos(state.anchor.x,state.anchor.y)){EndScrollCapture(true,L"无法定位滚动区域。");return;}
        state.capture.Start(GetTickCount64());
        RECT position=PlaceScrollPanel(state,capture.scale);
        if(PtInRect(&position,state.anchor)){EndScrollCapture(true,L"屏幕空间不足，无法放置滚动预览窗口。");return;}
        state.progress=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,ScrollClass,L"Imago — 滚动长截图",
            WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,position.left,position.top,position.right-position.left,position.bottom-position.top,
            app.controller,nullptr,app.instance,nullptr);
        if(!state.progress){EndScrollCapture(true,L"无法创建滚动控制窗口。");return;}
        ThemeWindow(state.progress,app.palette,true);state.font.reset(MakeFont(12,Scale(state.progress)));
        for(const auto& [id,label]:std::initializer_list<std::pair<UINT,const wchar_t*>>{
            {ScrollUpCommand,L"↑ 向上"},{ScrollDownCommand,L"↓ 向下"},{ScrollManualCommand,L"手动"},
            {FinishScrollCommand,L"完成"},{DiscardScrollCommand,L"取消"}}) {
            if(!Button(state.progress,label,id,state.font.get())){EndScrollCapture(true,L"无法创建滚动控制按钮。");return;}
        }
        LayoutScrollPanel(state.progress);
        RAWINPUTDEVICE device{1,2,RIDEV_INPUTSINK,app.controller};
        state.rawInputRegistered=RegisterRawInputDevices(&device,1,sizeof(device))!=FALSE;
        if(!state.rawInputRegistered)Notify(L"无法监听滚轮接管，请先点击「手动」再自行滚动。");
        state.escapeRegistered=RegisterHotKey(app.controller,ScrollStopHotkey,MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
        ShowWindow(state.progress,SW_SHOWNOACTIVATE);
        if(!SetTimer(app.controller,ScrollTimer,80,nullptr))EndScrollCapture(true,L"无法启动滚动采集定时器。");
    }catch(...) {if(app.scroll)EndScrollCapture(true,L"无法开始长截图：系统资源不足。");else if(app.overlay)ShowWindow(app.overlay,SW_SHOW);}
}
