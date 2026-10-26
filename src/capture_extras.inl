// Helpers for frozen desktop window selection and color sampling.
void UpdateWindowHover() {
    auto& c=*app.capture;
    c.hoverWindow=app.settings.windowSnap && !c.selected && !c.dragging && !(GetKeyState(VK_SHIFT)&0x8000)
        ?imago::WindowAt(c.windows,c.pointer):std::optional<RECT>{};
    InvalidateRect(app.overlay,nullptr,FALSE);
}
void ApplyCaptureColor(COLORREF chosen) {
    auto& c=*app.capture;c.color=chosen;
    bool redact=c.tool==CaptureTool::Redact;
    if(c.document.active && c.document.items[*c.document.active].kind!=imago::AnnotationKind::Mosaic) {
        c.document.Begin();auto& item=c.document.items[*c.document.active];item.color=chosen;
        redact=item.kind==imago::AnnotationKind::Redact;c.document.Commit();
    }
    if(redact)c.redactionColor=chosen;else c.drawingColor=chosen;
    RefreshToolbar();
}
void StartColorPicker() {
    auto& c=*app.capture;EndTextEdit(true);
    c.pickingColor=true;c.pickerCopied=false;
    POINT point{};GetCursorPos(&point);ScreenToClient(app.overlay,&point);c.pointer=point;
    HideToolbar();SetFocus(app.overlay);SetCursor(LoadCursorW(nullptr,IDC_CROSS));
    InvalidateRect(app.overlay,nullptr,FALSE);
}
COLORREF SampleColor() {
    const auto& c=*app.capture;
    const int x=std::clamp(static_cast<int>(c.pointer.x),0,c.desktop.width-1);
    const int y=std::clamp(static_cast<int>(c.pointer.y),0,c.desktop.height-1);
    const auto pixel=c.desktop.pixels[static_cast<std::size_t>(y)*c.desktop.width+x];
    return RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255);
}
std::wstring HexColor(COLORREF color) {
    wchar_t hex[8]{};swprintf_s(hex,L"#%02X%02X%02X",GetRValue(color),GetGValue(color),GetBValue(color));return hex;
}
void EndColorPicker(bool accept) {
    auto& c=*app.capture;
    if(accept)ApplyCaptureColor(SampleColor());
    c.pickingColor=false;LayoutToolbar();SetFocus(app.overlay);InvalidateRect(app.overlay,nullptr,FALSE);
}
void PaintColorPicker(Capture& c) {
    auto& canvas=c.canvas;const float s=c.scale;
    POINT screen{c.bounds.left+c.pointer.x,c.bounds.top+c.pointer.y};RECT work=MonitorWork(screen);
    OffsetRect(&work,-c.bounds.left,-c.bounds.top);
    const float width=224*s,height=174*s;
    float x=c.pointer.x+24*s,y=c.pointer.y+24*s;
    if(x+width>work.right-8*s)x=c.pointer.x-width-24*s;
    if(y+height>work.bottom-8*s)y=c.pointer.y-height-24*s;
    x=std::clamp(x,work.left+8*s,std::max(work.left+8*s,work.right-width-8*s));
    y=std::clamp(y,work.top+8*s,std::max(work.top+8*s,work.bottom-height-8*s));
    canvas.Round(D2D1::RectF(x,y,x+width,y+height),8*s,Color(app.palette.surface),Color(app.palette.border));
    const float cell=8*s,gx=x+12*s,gy=y+12*s;
    auto previous=canvas.target->GetAntialiasMode();canvas.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for(int row=0;row<11;++row)for(int column=0;column<11;++column) {
        const int px=std::clamp(static_cast<int>(c.pointer.x)+column-5,0,c.desktop.width-1);
        const int py=std::clamp(static_cast<int>(c.pointer.y)+row-5,0,c.desktop.height-1);
        const auto pixel=c.desktop.pixels[static_cast<std::size_t>(py)*c.desktop.width+px];
        canvas.Fill(D2D1::RectF(gx+column*cell,gy+row*cell,gx+(column+1)*cell,gy+(row+1)*cell),
            Color(RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255)));
    }
    canvas.target->SetAntialiasMode(previous);
    const auto center=D2D1::RectF(gx+5*cell,gy+5*cell,gx+6*cell,gy+6*cell);
    canvas.brush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));canvas.target->DrawRectangle(center,canvas.brush.Get(),3*s);
    canvas.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));canvas.target->DrawRectangle(center,canvas.brush.Get(),s);
    const auto color=SampleColor();
    canvas.Round(D2D1::RectF(x+114*s,y+12*s,x+width-12*s,y+50*s),4*s,Color(color),Color(app.palette.border));
    canvas.Text(HexColor(color),D2D1::RectF(x+114*s,y+56*s,x+width-8*s,y+80*s),15*s,app.palette.text,app.writeFactory.Get());
    const auto rgb=L"RGB  "+std::to_wstring(GetRValue(color))+L", "+std::to_wstring(GetGValue(color))+L", "+std::to_wstring(GetBValue(color));
    canvas.Text(rgb,D2D1::RectF(x+12*s,y+106*s,x+width-8*s,y+128*s),12*s,app.palette.text,app.writeFactory.Get());
    canvas.Text(L"单击选色 · Esc 返回",D2D1::RectF(x+12*s,y+130*s,x+width-8*s,y+148*s),11*s,app.palette.muted,app.writeFactory.Get());
    canvas.Text(c.pickerCopied?L"HEX 已复制":L"Ctrl+C 复制 HEX · 采样原始截图",D2D1::RectF(x+12*s,y+149*s,x+width-8*s,y+168*s),11*s,app.palette.muted,app.writeFactory.Get());
}
