// OCR runs on a worker; only this UI thread touches HWND state and the clipboard.
void CancelOcr(bool showStatus) {
    const HWND source=app.ocrSource;app.ocrSource=nullptr;
    app.ocr.Cancel();
    if(source && source==app.overlay && app.capture) {
        app.capture->ocrBusy=false;
        app.capture->ocrMessage=showStatus?L"已取消文字识别，截图已保留。":L"";
        RefreshToolbar();
    }else if(auto found=app.pins.find(source);found!=app.pins.end()) {
        found->second->ocrBusy=false;
        InvalidateRect(source,nullptr,FALSE);
    }else if(auto preview=app.longPreviews.find(source);preview!=app.longPreviews.end()) {
        preview->second->ocrBusy=false;
        if(showStatus)preview->second->note=L"已取消文字识别，长截图已保留。";
        InvalidateRect(source,nullptr,FALSE);
    }
}
bool StartOcrImage(Image image,HWND source) {
    CancelOcr(false);std::wstring error;
    if(!app.ocr.Start(std::move(image),app.controller,OcrCompleteMessage,error)){Notify(error.c_str(),true);return false;}
    app.ocrSource=source;return true;
}
void RecognizeCaptureText() {
    if(app.modal || !app.capture)return;
    auto& c=*app.capture;
    if(!c.selected || c.ocrBusy || c.dragging || c.pickingColor || c.annotationDrag!=AnnotationDrag::None)return;
    std::wstring error;Image image=CaptureOutput(error);
    if(!image.valid()){Notify(error.c_str(),true);return;}
    if(!StartOcrImage(std::move(image),app.overlay))return;
    c.ocrBusy=true;c.ocrMessage.clear();
    if(c.tooltip)SendMessageW(c.tooltip,TTM_POP,0,0);
    RefreshToolbar();SetFocus(app.overlay);
}
void RecognizePinText(HWND window) {
    if(app.modal || app.overlay)return;
    auto found=app.pins.find(window);
    if(found==app.pins.end() || found->second->ocrBusy)return;
    auto& pin=*found->second;
    if(!StartOcrImage(pin.image,window))return;
    pin.ocrBusy=true;InvalidateRect(window,nullptr,FALSE);
}
void CompleteOcr(WPARAM request) {
    auto result=app.ocr.TakeResult(request);if(!result)return;
    const HWND source=app.ocrSource;app.ocrSource=nullptr;
    Capture* capture=source && source==app.overlay?app.capture.get():nullptr;
    auto found=app.pins.find(source);Pin* pin=found==app.pins.end()?nullptr:found->second.get();
    auto longFound=app.longPreviews.find(source);LongPreview* preview=longFound==app.longPreviews.end()?nullptr:longFound->second.get();
    if(!capture && !pin && !preview)return;
    if(capture)capture->ocrBusy=false;else if(pin)pin->ocrBusy=false;else preview->ocrBusy=false;
    std::wstring status,error;
    if(result->status==imago::OcrStatus::Success) {
        if(imago::CopyText(app.controller,result->text,error)) {
            const auto notice=L"文字已复制（"+result->language+L"）"+(result->resized?L"。大图已按系统限制缩小识别。":L"。");
            if(preview)preview->note=notice;
            if(capture)CloseCapture(GetForegroundWindow()==source);
            else InvalidateRect(source,nullptr,FALSE);
            Notify(notice.c_str());return;
        }
        status=L"文字未能复制，请按 Ctrl+Shift+C 重试。";
    }else {
        error=result->error;
        switch(result->status) {
        case imago::OcrStatus::NoText:status=L"未识别到文字，可调整范围后重试。";break;
        case imago::OcrStatus::NoLanguage:status=L"缺少 Windows OCR 语言包。";break;
        case imago::OcrStatus::Cancelled:status=L"文字识别已取消。";break;
        default:status=L"文字识别未完成，图像已保留。";break;
        }
    }
    if(capture){capture->ocrMessage=status;RefreshToolbar();}
    else {if(preview)preview->note=status;InvalidateRect(source,nullptr,FALSE);}
    Notify(error.empty()?status.c_str():error.c_str(),result->status!=imago::OcrStatus::NoText);
}
void PaintCaptureOcrStatus(Capture& c) {
    if(!c.selected || (!c.ocrBusy && c.ocrMessage.empty()))return;
    auto& canvas=c.canvas;const float s=c.scale;
    POINT point{c.bounds.left+c.toolbar.left,c.bounds.top+c.toolbar.top};
    RECT work=MonitorWork(point);OffsetRect(&work,-c.bounds.left,-c.bounds.top);
    const float width=ToolbarWidth*s,height=60*s;
    const float x=static_cast<float>(c.toolbar.left);
    float y=c.toolbar.top-height-8*s;
    if(y<work.top+8*s)y=c.toolbar.bottom+8*s;
    y=std::clamp(y,work.top+8*s,std::max(work.top+8*s,work.bottom-height-8*s));
    canvas.Round(D2D1::RectF(x,y,x+width,y+height),6*s,Color(app.palette.surface),Color(app.palette.border));
    canvas.Text(c.ocrBusy?L"正在识别文字…":c.ocrMessage,D2D1::RectF(x+12*s,y+6*s,x+width-8*s,y+30*s),12*s,app.palette.text,app.writeFactory.Get());
    canvas.Text(c.ocrBusy?L"Windows 本机识别 · Esc 取消并返回编辑":L"Ctrl+Shift+C 识别文字 · Enter 复制图片",
        D2D1::RectF(x+12*s,y+30*s,x+width-8*s,y+53*s),11*s,app.palette.muted,app.writeFactory.Get());
}
void PaintPinOcrStatus(Pin& pin,RECT client) {
    if(!pin.ocrBusy || client.right<120 || client.bottom<32)return;
    const float bottom=static_cast<float>(client.bottom),width=static_cast<float>(client.right);
    pin.canvas.Fill(D2D1::RectF(0,bottom-30,width,bottom),Color(app.palette.surface));
    pin.canvas.Text(L"正在识别文字… Esc 取消",
        D2D1::RectF(8,bottom-30,width-6,bottom),12,app.palette.text,app.writeFactory.Get());
}
