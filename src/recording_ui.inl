// Recording mode shares the selection; completed clips live independently in the per-user cache.
bool ShowRecordingPanel() {
    if(!app.recordingPanel)return false;
    if(app.capture && !app.capture->selected && !app.recorder){SetForegroundWindow(app.overlay);return true;}
    app.recordingPanel->Show(true);SetForegroundWindow(app.recordingPanel->Window());
    SetFocus(GetNextDlgTabItem(app.recordingPanel->Window(),nullptr,FALSE));return true;
}
void LayoutRecordingPanel() {
    if(!app.recordingPanel || !app.capture || app.recorder)return;
    const auto& c=*app.capture;
    const bool visible=c.selected && !c.dragging;
    if(visible){RECT region=c.selection;OffsetRect(&region,c.bounds.left,c.bounds.top);app.recordingPanel->Region(region);}
    app.recordingPanel->Show(visible);
}
void EnterRecordingMode() {
    if(app.modal || app.scroll || app.recordingPanel || !app.capture)return;
    if(app.capture->ocrBusy || app.capture->dragging || app.capture->pickingColor || app.capture->annotationDrag!=AnnotationDrag::None)return;
    auto panel=std::make_unique<imago::RecordingPanel>();
    if(!panel->Create(app.instance,app.controller,RecordingPanelMessage,app.factory.Get(),app.palette,app.settings.animations)) {
        Notify(L"无法创建录屏工具栏。",true);return;
    }
    EndTextEdit(true);auto& c=*app.capture;c.screenshotTool=c.tool;c.tool=CaptureTool::Crop;c.recordingMode=true;
    c.ocrMessage.clear();app.recordingPanel=std::move(panel);LayoutToolbar();InvalidateRect(app.overlay,nullptr,FALSE);
}
void BackToScreenshot() {
    if(app.recorder || !app.capture)return;
    app.recordingPanel.reset();auto& c=*app.capture;c.recordingMode=false;c.tool=c.screenshotTool;
    if(c.dragging){c.dragging=false;c.selection=c.original;c.selected=c.selection.right>c.selection.left && c.selection.bottom>c.selection.top;ReleaseCapture();}
    ShowWindow(app.overlay,SW_SHOWNOACTIVATE);LayoutToolbar();RefreshToolbar();SetForegroundWindow(app.overlay);SetFocus(app.overlay);
}
void OpenRecordingFolder() {
    std::wstring error;const auto folder=imago::RecordingFolder(error);
    if(folder.empty()){Notify(error.c_str(),true);return;}
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(app.controller,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        Notify(L"无法打开录像暂存目录。",true);
}
std::wstring RecordingSavePath(HWND owner) {
    ComPtr<IFileSaveDialog> dialog;
    HRESULT result=CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(FAILED(result)){MessageBoxW(owner,L"无法打开 Windows 保存对话框。",L"Imago 录屏",MB_OK|MB_ICONERROR);return {};}
    const COMDLG_FILTERSPEC types[]{{L"MP4 视频 (*.mp4)",L"*.mp4"}};
    dialog->SetFileTypes(1,types);dialog->SetDefaultExtension(L"mp4");dialog->SetTitle(L"保存录像");
    DWORD flags{};dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_OVERWRITEPROMPT|FOS_STRICTFILETYPES);
    constexpr GUID identity{0x430d872f,0xb091,0x4bad,{0xa1,0xd4,0xda,0x8c,0x43,0xea,0x9a,0x80}};
    dialog->SetClientGuid(identity);
    ComPtr<IShellItem> folder;
    if(SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Videos,KF_FLAG_DEFAULT,nullptr,IID_PPV_ARGS(&folder))))dialog->SetDefaultFolder(folder.Get());
    SYSTEMTIME time{};GetLocalTime(&time);wchar_t name[80]{};
    swprintf_s(name,L"Imago-%04u%02u%02u-%02u%02u%02u.mp4",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);dialog->SetFileName(name);
    result=dialog->Show(owner);
    if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
    ComPtr<IShellItem> file;PWSTR path=nullptr;
    if(FAILED(result) || FAILED(dialog->GetResult(&file)) || FAILED(file->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
        MessageBoxW(owner,L"无法读取录像保存位置。",L"Imago 录屏",MB_OK|MB_ICONERROR);return {};
    }
    struct FreePath{PWSTR value;~FreePath(){CoTaskMemFree(value);}} freePath{path};
    return path;
}
void CloseRecordingResult() {
    if(app.recordingExport){app.recordingExport->Cancel();app.recordingExport->Join();app.recordingExport.reset();}
    KillTimer(app.controller,RecordingTimer);app.recordingResult.reset();app.recordingPanel.reset();
}
void BeginRecording() {
    if(!app.recordingPanel || app.recorder || !app.capture || !app.capture->selected || app.capture->dragging)return;
    const auto options=app.recordingPanel->Options();std::wstring error;
    if(!imago::ValidateRecording(options,error)) {MessageBoxW(app.recordingPanel->Window(),error.c_str(),L"Imago 录屏",MB_OK|MB_ICONWARNING);return;}
    if(!app.recordingPanel->ExcludedFromCapture()) {
        MessageBoxW(app.recordingPanel->Window(),L"系统无法排除录屏控制条，请检查桌面合成和系统版本。",L"Imago 录屏",MB_OK|MB_ICONWARNING);return;
    }
    const auto path=imago::NewRecordingPath(error);
    if(path.empty()){MessageBoxW(app.recordingPanel->Window(),error.c_str(),L"Imago 录屏",MB_OK|MB_ICONERROR);return;}
    auto border=std::make_unique<imago::RecordingBorder>();
    if(!border->Create(app.instance,app.controller,options.region,app.palette)) {
        MessageBoxW(app.recordingPanel->Window(),L"无法创建录屏范围提示，请检查桌面合成状态。",L"Imago 录屏",MB_OK|MB_ICONERROR);return;
    }
    auto recorder=std::make_unique<imago::Recorder>();
    if(!SetTimer(app.controller,RecordingTimer,100,nullptr)){Notify(L"无法启动录屏状态计时器。",true);return;}
    ShowWindow(app.overlay,SW_HIDE);border->Show();DwmFlush();
    if(!recorder->Start(options,path,error)) {
        border.reset();KillTimer(app.controller,RecordingTimer);ShowWindow(app.overlay,SW_SHOWNOACTIVATE);LayoutToolbar();
        MessageBoxW(app.recordingPanel->Window(),error.c_str(),L"Imago 录屏",MB_OK|MB_ICONERROR);return;
    }
    app.exitRecording=false;app.recordingBorder=std::move(border);app.recorder=std::move(recorder);
    app.recordingPanel->Active(app.recorder->Status());app.recordingPanel->Show(true);
    if(app.previousForeground && IsWindow(app.previousForeground) && IsWindowVisible(app.previousForeground))SetForegroundWindow(app.previousForeground);
}
void SaveRecordingResult() {
    if(!app.recordingResult || app.recordingExport)return;
    std::wstring path;
    {CaptureModalScope modal;path=RecordingSavePath(app.recordingPanel->Window());}
    if(path.empty())return;
    auto task=std::make_unique<imago::RecordingExport>();std::wstring error;
    if(!SetTimer(app.controller,RecordingTimer,100,nullptr)){Notify(L"无法启动录像保存任务。",true);return;}
    if(!task->Start(app.recordingResult->path,path,error)){KillTimer(app.controller,RecordingTimer);Notify(error.c_str(),true);return;}
    app.recordingExport=std::move(task);app.recordingPanel->Exporting(0);
}
void HandleRecordingAction(imago::RecordingAction action) {
    if(app.modal || !app.recordingPanel)return;
    using Action=imago::RecordingAction;
    if(app.recorder) {
        if(action==Action::Stop || action==Action::Close) {
            app.exitRecording=app.exitRecording || action==Action::Close;app.recorder->Stop();app.recordingBorder.reset();
        }else if(action==Action::Pause) {
            const auto state=app.recorder->Status().state;
            if(state==imago::RecordingState::Recording || state==imago::RecordingState::Paused)app.recorder->SetPaused(state!=imago::RecordingState::Paused);
        }
        return;
    }
    if(app.recordingResult) {
        if(action==Action::Close){CloseRecordingResult();return;}
        if(app.recordingExport)return;
        if(action==Action::Copy) {
            std::wstring error;
            if(imago::CopyRecordingFile(app.controller,app.recordingResult->path,error)){CloseRecordingResult();Notify(L"视频文件已复制，可粘贴到文件夹或支持文件粘贴的应用。");}
            else Notify(error.c_str(),true);
        }else if(action==Action::Save)SaveRecordingResult();
        else if(action==Action::Preview && reinterpret_cast<INT_PTR>(ShellExecuteW(app.recordingPanel->Window(),L"open",app.recordingResult->path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
            Notify(L"无法打开播放器，可复制或保存录像后再播放。",true);
        return;
    }
    if(action==Action::Close)CloseCapture(true);
    else if(action==Action::Back)BackToScreenshot();
    else if(action==Action::Start)BeginRecording();
    else if(action==Action::Reselect && app.capture) {
        auto& c=*app.capture;c.selected=false;c.selection={};c.pendingSnap.reset();c.hoverWindow.reset();
        LayoutToolbar();InvalidateRect(app.overlay,nullptr,FALSE);SetForegroundWindow(app.overlay);SetFocus(app.overlay);
    }
}
void RecordingTick() {
    if(app.recordingExport) {
        const auto progress=app.recordingExport->Status();
        if(!progress.done){app.recordingPanel->Exporting(progress.percent);return;}
        app.recordingExport->Join();app.recordingExport.reset();KillTimer(app.controller,RecordingTimer);
        if(progress.saved){CloseRecordingResult();Notify((L"录像已保存：\n"+progress.path).c_str());}
        else {
            app.recordingPanel->Result(*app.recordingResult);
            CaptureModalScope modal;MessageBoxW(app.recordingPanel->Window(),progress.error.c_str(),L"Imago 录屏",MB_OK|MB_ICONWARNING);
        }
        return;
    }
    if(!app.recorder)return;
    const auto status=app.recorder->Status();using State=imago::RecordingState;
    if(status.state!=State::Completed && status.state!=State::Cancelled && status.state!=State::Failed) {
        if(app.recordingPanel)app.recordingPanel->Active(status);
        if(status.state==State::Finalizing)app.recordingBorder.reset();
        else if(app.recordingBorder)app.recordingBorder->Appearance(app.palette,status.state==State::Paused);
        return;
    }
    KillTimer(app.controller,RecordingTimer);app.recorder->Join();app.recorder.reset();app.recordingBorder.reset();
    const auto attributes=GetFileAttributesW(status.path.c_str());
    const bool hasFile=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
    if(app.exitRecording) {
        CloseCapture(true);
        if(hasFile)Notify((status.error.empty()?L"录屏已退出，录像保留在暂存目录，可从托盘菜单打开。":status.error+L" 录像保留在暂存目录。").c_str(),!status.error.empty());
        else if(!status.error.empty())Notify(status.error.c_str(),true);
        return;
    }
    if(status.state==State::Cancelled || !hasFile) {
        BackToScreenshot();
        if(!status.error.empty()){CaptureModalScope modal;MessageBoxW(app.overlay,status.error.c_str(),L"Imago 录屏",MB_OK|MB_ICONERROR);}
        return;
    }
    // Release the frozen screenshot, retaining the independent result toolbar and cached file.
    auto panel=std::move(app.recordingPanel);CloseCapture(false);app.recordingPanel=std::move(panel);
    app.recordingResult=status;app.recordingPanel->Result(status);ShowRecordingPanel();
    if(!status.error.empty()) {
        CaptureModalScope modal;MessageBoxW(app.recordingPanel->Window(),(status.error+L"\n\n录像位置：\n"+status.path).c_str(),L"Imago 录屏",MB_OK|MB_ICONWARNING);
    }
}
void ShutdownRecording() {
    if(app.controller)KillTimer(app.controller,RecordingTimer);
    app.recordingBorder.reset();
    if(app.recorder){app.recorder->Stop();app.recorder->Join();app.recorder.reset();}
    if(app.recordingExport){app.recordingExport->Cancel();app.recordingExport->Join();app.recordingExport.reset();}
    app.recordingResult.reset();app.recordingPanel.reset();
}
