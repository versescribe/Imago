// Update checks only publish metadata; opening the release page always requires a user action.
std::wstring UpdateButtonText() {
    if(app.updates.Busy())return L"正在检查…";
    if(app.updateResult && app.updateResult->state==imago::UpdateState::Available)return L"发现新版本";
    return L"检查更新";
}
void RefreshUpdateButton() {
    if(!app.welcome)return;
    const auto button=GetDlgItem(app.welcome,UpdateCommand);
    if(button){SetWindowTextW(button,UpdateButtonText().c_str());EnableWindow(button,!app.updates.Busy());}
}
void ShowUpdateResult() {
    if(!app.updateResult || app.modal)return;
    const auto result=*app.updateResult;
    const bool available=result.state==imago::UpdateState::Available;
    std::wstring title,content=L"当前版本：" IMAGO_VERSION_W;
    if(available){title=L"发现 Imago "+result.version;content+=L"\n最新版本："+result.version+L"\n\n打开 GitHub Release 查看说明并下载新版。\n退出托盘中的旧版后，再替换程序文件。";}
    else if(result.state==imago::UpdateState::Current)title=L"当前已是最新版本";
    else {title=L"暂时无法确认更新";content+=L"\n\n"+result.error;}
    const TASKDIALOG_BUTTON buttons[]{{100,L"打开 GitHub 下载页"}};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent=app.welcome && IsWindowVisible(app.welcome)?app.welcome:app.controller;
    config.hInstance=app.instance;config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons=TDCBF_CLOSE_BUTTON;config.pszWindowTitle=L"Imago 更新";
    config.pszMainInstruction=title.c_str();config.pszContent=content.c_str();
    config.pszMainIcon=available?TD_INFORMATION_ICON:result.state==imago::UpdateState::Current?TD_INFORMATION_ICON:TD_WARNING_ICON;
    config.cButtons=1;config.pButtons=buttons;config.nDefaultButton=IDCLOSE;
    int clicked{};HRESULT shown{};
    {CaptureModalScope modal;shown=TaskDialogIndirect(&config,&clicked,nullptr,nullptr);}
    if(FAILED(shown)){Notify(content.c_str(),true);return;}
    if(clicked==100 && reinterpret_cast<INT_PTR>(ShellExecuteW(app.controller,L"open",imago::ReleasesPage,nullptr,nullptr,SW_SHOWNORMAL))<=32)
        Notify(L"无法打开浏览器，请访问 github.com/versescribe/Imago/releases。",true);
}
void PresentUpdateNotice() {
    if(!app.updateNotice){KillTimer(app.controller,UpdateNoticeTimer);return;}
    if(app.modal || app.overlay || app.recordingPanel || app.scroll)return;
    app.updateNotice=false;KillTimer(app.controller,UpdateNoticeTimer);
    if(app.manualUpdate){app.manualUpdate=false;ShowUpdateResult();}
    else if(app.settings.checkUpdates && app.updateResult && app.updateResult->state==imago::UpdateState::Available)
        Notify((L"发现 Imago "+app.updateResult->version+L"。可从主窗口或托盘的“发现新版本”打开下载页。").c_str());
}
void CheckUpdates(bool manual) {
    if(app.modal)return;
    if(app.updates.Busy()){app.manualUpdate=app.manualUpdate || manual;return;}
    if(manual && app.updateResult && app.updateResult->state==imago::UpdateState::Available) {
        app.manualUpdate=true;app.updateNotice=true;
        SetTimer(app.controller,UpdateNoticeTimer,500,nullptr);PresentUpdateNotice();return;
    }
    if(!app.updates.Start(app.controller,UpdateCompleteMessage)){if(manual)Notify(L"无法启动更新检查，请重试。",true);return;}
    app.manualUpdate=manual;app.updateNotice=false;KillTimer(app.controller,UpdateNoticeTimer);
    imago::RememberUpdateAttempt();RefreshUpdateButton();
}
void CompleteUpdateCheck() {
    auto result=app.updates.TakeResult();if(!result)return;
    app.updateResult=std::move(*result);RefreshUpdateButton();
    app.updateNotice=app.manualUpdate || app.updateResult->state==imago::UpdateState::Available;
    if(app.updateNotice){SetTimer(app.controller,UpdateNoticeTimer,500,nullptr);PresentUpdateNotice();}
}
