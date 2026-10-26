#include "toolbar.h"
#include <algorithm>
#include <initializer_list>

namespace imago {
namespace {
D2D1_COLOR_F Color(COLORREF color) {
    return D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f);
}
}
void ToolbarRenderer::Reset() { brush_.Reset(); target_.Reset(); stroke_.Reset(); }
bool ToolbarRenderer::Draw(HDC dc,const RECT& bounds,ToolbarIcon icon,const IconButtonStyle& style,ID2D1Factory* factory) {
    if(!target_) {
        auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        HRESULT hr=factory->CreateDCRenderTarget(&properties,&target_);
        if(SUCCEEDED(hr))hr=target_->CreateSolidColorBrush(Color(style.foreground),&brush_);
        auto stroke=D2D1::StrokeStyleProperties();
        stroke.startCap=stroke.endCap=stroke.dashCap=D2D1_CAP_STYLE_ROUND;
        stroke.lineJoin=D2D1_LINE_JOIN_ROUND;
        if(SUCCEEDED(hr))hr=factory->CreateStrokeStyle(stroke,nullptr,0,&stroke_);
        if(FAILED(hr)){Reset();return false;}
    }
    if(FAILED(target_->BindDC(dc,&bounds))){Reset();return false;}
    const float width=static_cast<float>(bounds.right-bounds.left),height=static_cast<float>(bounds.bottom-bounds.top);
    const float scale=height/32.f;
    target_->BeginDraw();target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(Color(style.background));
    auto button=D2D1::RoundedRect(D2D1::RectF(.5f,.5f,width-.5f,height-.5f),4*scale,4*scale);
    brush_->SetColor(Color(style.fill));target_->FillRoundedRectangle(button,brush_.Get());
    brush_->SetColor(Color(style.border));target_->DrawRoundedRectangle(button,brush_.Get());
    const float size=20*scale,contentWidth=width-(style.dropdown?8*scale:0);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(size/24,size/24)*
        D2D1::Matrix3x2F::Translation((contentWidth-size)/2,(height-size)/2+(style.pressed?scale:0)));
    brush_->SetColor(Color(style.foreground));Icon(icon,style.swatch);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    brush_->SetColor(Color(style.foreground));
    if(style.dropdown) {
        const float x=width-7*scale,y=height/2;
        const float direction=style.dropdownUp?-1.f:1.f;
        target_->DrawLine({x-2.5f*scale,y-direction*scale},{x,y+direction*1.5f*scale},brush_.Get(),1.3f*scale,stroke_.Get());
        target_->DrawLine({x,y+direction*1.5f*scale},{x+2.5f*scale,y-direction*scale},brush_.Get(),1.3f*scale,stroke_.Get());
    }
    if(style.focused) {
        brush_->SetColor(Color(style.focus));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2*scale,2*scale,width-2*scale,height-2*scale),3*scale,3*scale),brush_.Get(),scale);
    }
    if(FAILED(target_->EndDraw())){Reset();return false;}
    return true;
}
void ToolbarRenderer::Icon(ToolbarIcon icon,COLORREF swatch) {
    auto line=[&](float x1,float y1,float x2,float y2) {
        target_->DrawLine({x1,y1},{x2,y2},brush_.Get(),1.8f,stroke_.Get());
    };
    auto path=[&](std::initializer_list<D2D1_POINT_2F> points) {
        if(points.size()<2)return;
        auto previous=points.begin();
        for(auto point=previous+1;point!=points.end();++point){line(previous->x,previous->y,point->x,point->y);previous=point;}
    };
    auto rect=[&](float left,float top,float right,float bottom) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left,top,right,bottom),1,1),brush_.Get(),1.8f,stroke_.Get());
    };
    switch(icon) {
    case ToolbarIcon::Select:
        path({{5,3},{5,19},{9,15},{12,21},{15,19.5f},{12,14},{18,13},{5,3}});break;
    case ToolbarIcon::Crop:
        path({{7,3},{7,17},{21,17}});path({{3,7},{17,7},{17,21}});break;
    case ToolbarIcon::Rectangle:rect(4,5,20,19);break;
    case ToolbarIcon::Ellipse:target_->DrawEllipse(D2D1::Ellipse({12,12},8,8),brush_.Get(),1.8f);break;
    case ToolbarIcon::Arrow:line(4,12,20,12);path({{14,6},{20,12},{14,18}});break;
    case ToolbarIcon::Text:path({{4,6},{4,4},{20,4},{20,6}});line(12,4,12,20);line(8,20,16,20);break;
    case ToolbarIcon::Redact:target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(3,7,21,17),2,2),brush_.Get());break;
    case ToolbarIcon::Mosaic:
        for(int y=0;y<3;++y)for(int x=0;x<3;++x) {
            auto cell=D2D1::RectF(4.f+x*6,4.f+y*6,8.f+x*6,8.f+y*6);
            if((x+y)%2==0)target_->FillRectangle(cell,brush_.Get());else target_->DrawRectangle(cell,brush_.Get(),1);
        }break;
    case ToolbarIcon::Blur:
        for(int y=0;y<3;++y)for(int x=0;x<3;++x) {
            brush_->SetOpacity(x==1 && y==1?1.f:(x==1 || y==1)?.6f:.3f);
            target_->FillEllipse(D2D1::Ellipse({6.f+x*6,6.f+y*6},2.5f,2.5f),brush_.Get());
        }
        brush_->SetOpacity(1.f);break;
    case ToolbarIcon::Color:{
        const auto foreground=brush_->GetColor();
        brush_->SetColor(Color(swatch));target_->FillEllipse(D2D1::Ellipse({12,12},8,8),brush_.Get());
        brush_->SetColor(foreground);target_->DrawEllipse(D2D1::Ellipse({12,12},8,8),brush_.Get(),1.3f);break;
    }
    case ToolbarIcon::Picker:
        path({{14,4},{20,10},{17,13},{11,7},{14,4}});
        path({{12,8},{4,16},{3,21},{8,20},{16,12}});line(11,5,19,13);break;
    case ToolbarIcon::Undo:case ToolbarIcon::Redo:{
        // Mirror the same path so the two history actions remain visually balanced.
        const bool redo=icon==ToolbarIcon::Redo;
        auto x=[&](float value){return redo?24-value:value;};
        path({{x(8),4},{x(3),9},{x(8),14}});
        path({{x(3),9},{x(14),9},{x(18),10},{x(20),13},{x(20),16},{x(18),19},{x(15),20}});break;
    }
    case ToolbarIcon::Pin:path({{8,3},{16,3},{15,11},{19,15},{5,15},{9,11},{8,3}});line(12,15,12,21);break;
    case ToolbarIcon::Ocr:
        path({{3,8},{3,3},{8,3}});path({{16,3},{21,3},{21,8}});
        path({{3,16},{3,21},{8,21}});path({{16,21},{21,21},{21,16}});
        line(8,8,16,8);line(12,8,12,16);line(9,16,15,16);break;
    case ToolbarIcon::Scroll:
        line(7,20,7,4);path({{3,8},{7,4},{11,8}});
        line(17,4,17,20);path({{13,16},{17,20},{21,16}});break;
    case ToolbarIcon::Record:rect(3,6,15,18);path({{15,10},{21,6},{21,18},{15,14}});break;
    case ToolbarIcon::Screenshot:
        path({{3,7},{7,7},{9,4},{15,4},{17,7},{21,7},{21,20},{3,20},{3,7}});
        target_->DrawEllipse(D2D1::Ellipse({12,13},4,4),brush_.Get(),1.8f);break;
    case ToolbarIcon::Speaker:case ToolbarIcon::SpeakerMuted:
        path({{3,9},{7,9},{12,5},{12,19},{7,15},{3,15},{3,9}});
        if(icon==ToolbarIcon::Speaker){path({{16,8},{18,10},{18,14},{16,16}});path({{19,5},{22,9},{22,15},{19,19}});}
        else {line(16,9,21,15);line(21,9,16,15);}break;
    case ToolbarIcon::Microphone:case ToolbarIcon::MicrophoneMuted:
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(9,3,15,15),3,3),brush_.Get(),1.8f);
        path({{6,11},{6,15},{9,18},{15,18},{18,15},{18,11}});line(12,18,12,21);line(8,21,16,21);
        if(icon==ToolbarIcon::MicrophoneMuted)line(3,3,21,21);break;
    case ToolbarIcon::Resolution:rect(3,4,21,17);line(12,17,12,21);line(8,21,16,21);break;
    case ToolbarIcon::FrameRate:
        target_->DrawEllipse(D2D1::Ellipse({12,13},8,8),brush_.Get(),1.8f);line(9,2,15,2);line(12,2,12,5);
        line(12,13,16,9);target_->FillEllipse(D2D1::Ellipse({12,13},1.5f,1.5f),brush_.Get());break;
    case ToolbarIcon::StartRecording:target_->FillEllipse(D2D1::Ellipse({12,12},7,7),brush_.Get());break;
    case ToolbarIcon::Pause:
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(5,4,9,20),1,1),brush_.Get());
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(15,4,19,20),1,1),brush_.Get());break;
    case ToolbarIcon::Play:path({{7,4},{20,12},{7,20},{7,4}});break;
    case ToolbarIcon::Stop:target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(5,5,19,19),1,1),brush_.Get());break;
    case ToolbarIcon::Save:path({{5,3},{17,3},{21,7},{21,21},{3,21},{3,3},{5,3}});rect(7,3,15,9);rect(7,14,17,21);break;
    case ToolbarIcon::Copy:rect(8,8,20,21);path({{16,5},{16,3},{3,3},{3,16},{5,16}});break;
    case ToolbarIcon::Cancel:line(6,6,18,18);line(18,6,6,18);break;
    }
}
}
