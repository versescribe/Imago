#include "annotations.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace imago {
void AnnotationDocument::Trim(std::vector<Snapshot>& stack) {
    std::size_t bytes = 0;
    for (const auto& snapshot : stack) {
        bytes += snapshot.size() * sizeof(Annotation);
        for (const auto& item : snapshot) bytes += item.text.size() * sizeof(wchar_t);
    }
    while (stack.size() > 1 && (stack.size() > 100 || bytes > 8u * 1024u * 1024u)) {
        bytes -= stack.front().size() * sizeof(Annotation);
        for (const auto& item : stack.front()) bytes -= item.text.size() * sizeof(wchar_t);
        stack.erase(stack.begin());
    }
}
void AnnotationDocument::Begin() { if (!before_) before_ = items; }
void AnnotationDocument::Commit() {
    if (!before_) return;
    if (*before_ != items) {
        undo_.push_back(std::move(*before_));
        redo_.clear();
        Trim(undo_);
    }
    before_.reset();
}
void AnnotationDocument::Cancel() {
    if (before_) { items = std::move(*before_); before_.reset(); }
    if (active && *active >= items.size()) active.reset();
}
void AnnotationDocument::Undo() {
    Commit();
    if (!CanUndo()) return;
    redo_.push_back(std::move(items));
    items = std::move(undo_.back()); undo_.pop_back(); active.reset(); Trim(redo_);
}
void AnnotationDocument::Redo() {
    Commit();
    if (!CanRedo()) return;
    undo_.push_back(std::move(items));
    items = std::move(redo_.back()); redo_.pop_back(); active.reset(); Trim(undo_);
}
void AnnotationDocument::DeleteActive() {
    if (!active || *active >= items.size()) return;
    Begin(); items.erase(items.begin() + static_cast<std::ptrdiff_t>(*active)); active.reset(); Commit();
}
D2D1_RECT_F AnnotationBounds(const Annotation& item) {
    return D2D1::RectF(std::min(item.x1,item.x2), std::min(item.y1,item.y2),
                       std::max(item.x1,item.x2), std::max(item.y1,item.y2));
}
namespace {
float Distance(D2D1_POINT_2F a, D2D1_POINT_2F b) { return std::hypot(a.x-b.x,a.y-b.y); }
float SegmentDistance(D2D1_POINT_2F p, D2D1_POINT_2F a, D2D1_POINT_2F b) {
    const float dx=b.x-a.x, dy=b.y-a.y, length=dx*dx+dy*dy;
    const float t=length>0 ? std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.f,1.f) : 0.f;
    return Distance(p,{a.x+t*dx,a.y+t*dy});
}
bool Contains(D2D1_RECT_F r,D2D1_POINT_2F p,float tolerance=0) {
    return p.x>=r.left-tolerance && p.x<=r.right+tolerance && p.y>=r.top-tolerance && p.y<=r.bottom+tolerance;
}
std::pair<D2D1_POINT_2F,D2D1_POINT_2F> ArrowWings(const Annotation& item) {
    const float dx=item.x2-item.x1,dy=item.y2-item.y1,length=std::hypot(dx,dy);
    if(length<.1f)return {{item.x2,item.y2},{item.x2,item.y2}};
    const float head=std::min(length*.45f,std::max(12.f,item.stroke*4.f));
    const float x=item.x2-head*dx/length,y=item.y2-head*dy/length;
    return {{x-head*.48f*dy/length,y+head*.48f*dx/length},
            {x+head*.48f*dy/length,y-head*.48f*dx/length}};
}
D2D1_COLOR_F Color(COLORREF value) {
    return D2D1::ColorF(GetRValue(value)/255.f,GetGValue(value)/255.f,GetBValue(value)/255.f,1.f);
}
}
int HitAnnotationHandle(const Annotation& item,D2D1_POINT_2F p,float tolerance) {
    if(item.kind==AnnotationKind::Arrow) {
        if(Distance(p,{item.x1,item.y1})<=tolerance)return 1;
        if(Distance(p,{item.x2,item.y2})<=tolerance)return 4;
        return 0;
    }
    const auto b=AnnotationBounds(item);
    if(!Contains(b,p,tolerance))return 0;
    int result=0;
    if(std::abs(p.x-b.left)<=tolerance)result|=1;
    else if(std::abs(p.x-b.right)<=tolerance)result|=4;
    if(std::abs(p.y-b.top)<=tolerance)result|=2;
    else if(std::abs(p.y-b.bottom)<=tolerance)result|=8;
    return result;
}
std::optional<std::size_t> HitAnnotation(const std::vector<Annotation>& items,D2D1_POINT_2F point,float tolerance) {
    // Pixel effects are below vector annotations regardless of creation order.
    for(bool mosaicPass:{false,true}) {
    for(std::size_t i=items.size();i>0;--i) {
        const auto& item=items[i-1];const auto bounds=AnnotationBounds(item);
        if((item.kind==AnnotationKind::Mosaic)!=mosaicPass)continue;
        if(item.kind==AnnotationKind::Arrow) {
            const auto wings=ArrowWings(item);
            if(SegmentDistance(point,{item.x1,item.y1},{item.x2,item.y2})<=tolerance+item.stroke/2 ||
               SegmentDistance(point,wings.first,{item.x2,item.y2})<=tolerance+item.stroke/2 ||
               SegmentDistance(point,wings.second,{item.x2,item.y2})<=tolerance+item.stroke/2)return i-1;
        } else if(item.kind==AnnotationKind::Ellipse) {
            const float rx=(bounds.right-bounds.left)/2,ry=(bounds.bottom-bounds.top)/2;
            const float dx=point.x-(bounds.left+bounds.right)/2,dy=point.y-(bounds.top+bounds.bottom)/2;
            const float padding=tolerance+item.stroke/2;
            const auto radius=[&](float x,float y){return dx*dx/(x*x)+dy*dy/(y*y);};
            if(rx>0 && ry>0 && radius(rx+padding,ry+padding)<=1 &&
               (rx<=padding || ry<=padding || radius(rx-padding,ry-padding)>=1))return i-1;
        } else if(Contains(bounds,point,tolerance)) {
            if(item.kind!=AnnotationKind::Rectangle || HitAnnotationHandle(item,point,tolerance+item.stroke/2))return i-1;
        }
    }
    }
    return {};
}
const Image& MosaicCache::Get(const Image& source,int blockSize,MosaicStyle style) {
    if(source_!=source.pixels.data()){images_.clear();source_=source.pixels.data();}
    blockSize=std::clamp(blockSize,8,64);
    const auto key=std::make_pair(style,blockSize);
    if(auto found=images_.find(key);found!=images_.end())return found->second;
    Image image;
    image.width=(source.width+blockSize-1)/blockSize;image.height=(source.height+blockSize-1)/blockSize;
    image.pixels.resize(static_cast<std::size_t>(image.width)*image.height);
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        unsigned red=0,green=0,blue=0,count=0;
        for(int sy=y*blockSize;sy<std::min(source.height,(y+1)*blockSize);++sy)
            for(int sx=x*blockSize;sx<std::min(source.width,(x+1)*blockSize);++sx) {
                const auto pixel=source.pixels[static_cast<std::size_t>(sy)*source.width+sx];
                red+=(pixel>>16)&255;green+=(pixel>>8)&255;blue+=pixel&255;++count;
            }
        image.pixels[static_cast<std::size_t>(y)*image.width+x]=0xff000000u|((red/count)<<16)|((green/count)<<8)|(blue/count);
    }
    if(style==MosaicStyle::Blur) {
        // Three separable box passes approximate a smooth blur on the reduced source.
        // Keep the cached images small; both preview and export use these exact pixels.
        std::vector<std::uint32_t> scratch(image.pixels.size());
        for(int pass=0;pass<3;++pass)for(bool vertical:{false,true}) {
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
                unsigned red=0,green=0,blue=0;
                for(int offset=-1;offset<=1;++offset) {
                    const int sx=std::clamp(x+(vertical?0:offset),0,image.width-1);
                    const int sy=std::clamp(y+(vertical?offset:0),0,image.height-1);
                    const auto pixel=image.pixels[static_cast<std::size_t>(sy)*image.width+sx];
                    red+=(pixel>>16)&255;green+=(pixel>>8)&255;blue+=pixel&255;
                }
                scratch[static_cast<std::size_t>(y)*image.width+x]=0xff000000u|((red/3)<<16)|((green/3)<<8)|(blue/3);
            }
            image.pixels.swap(scratch);
        }
    }
    return images_.emplace(key,std::move(image)).first->second;
}
HRESULT DrawAnnotations(ID2D1RenderTarget* target,IDWriteFactory* textFactory,const Image& source,MosaicCache& mosaics,
                        const std::vector<Annotation>& items,std::optional<std::size_t> exclude) {
    if(items.empty())return S_OK;
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT hr=target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red),&brush);
    if(FAILED(hr))return hr;
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    std::map<std::pair<MosaicStyle,int>,ComPtr<ID2D1Bitmap>> bitmaps;
    for(const auto& item:items) {
        if(item.kind!=AnnotationKind::Mosaic)continue;
        const int block=std::clamp(item.blockSize,8,64);
        auto& bitmap=bitmaps[{item.mosaicStyle,block}];
        if(!bitmap) {
            const auto& image=mosaics.Get(source,block,item.mosaicStyle);
            hr=target->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),image.width*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96),&bitmap);
            if(FAILED(hr))return hr;
        }
        const auto bounds=AnnotationBounds(item);const auto size=bitmap->GetSize();
        target->PushAxisAlignedClip(D2D1::RectF(std::floor(bounds.left),std::floor(bounds.top),std::ceil(bounds.right),std::ceil(bounds.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);
        target->DrawBitmap(bitmap.Get(),D2D1::RectF(0,0,size.width*block,size.height*block),1,
            item.mosaicStyle==MosaicStyle::Blur?D2D1_BITMAP_INTERPOLATION_MODE_LINEAR:D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        target->PopAxisAlignedClip();
    }
    for(std::size_t i=0;i<items.size();++i) {
        if(exclude && *exclude==i)continue;
        const auto& item=items[i];const auto bounds=AnnotationBounds(item);
        brush->SetColor(Color(item.color));
        switch(item.kind) {
        case AnnotationKind::Mosaic:break;
        case AnnotationKind::Rectangle:target->DrawRectangle(bounds,brush.Get(),item.stroke);break;
        case AnnotationKind::Ellipse:
            target->DrawEllipse(D2D1::Ellipse({(bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2},
                (bounds.right-bounds.left)/2,(bounds.bottom-bounds.top)/2),brush.Get(),item.stroke);break;
        case AnnotationKind::Arrow:{
            const auto wings=ArrowWings(item);
            target->DrawLine({item.x1,item.y1},{item.x2,item.y2},brush.Get(),item.stroke);
            target->DrawLine(wings.first,{item.x2,item.y2},brush.Get(),item.stroke);
            target->DrawLine(wings.second,{item.x2,item.y2},brush.Get(),item.stroke);break;
        }
        case AnnotationKind::Redact:{
            // Opaque, pixel-aligned coverage: no source pixels or editable layers are exported beneath this block.
            auto previous=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            target->FillRectangle(D2D1::RectF(std::floor(bounds.left),std::floor(bounds.top),std::ceil(bounds.right),std::ceil(bounds.bottom)),brush.Get());
            target->SetAntialiasMode(previous);break;
        }
        case AnnotationKind::Text:{
            ComPtr<IDWriteTextFormat> format;
            hr=textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,item.fontSize,L"zh-CN",&format);
            if(FAILED(hr))return hr;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            target->DrawTextW(item.text.c_str(),static_cast<UINT32>(item.text.size()),format.Get(),bounds,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
            break;
        }
        }
    }
    return S_OK;
}
Image Composite(const Image& desktop,RECT crop,const std::vector<Annotation>& items,
                ID2D1Factory* factory,IDWriteFactory* textFactory,MosaicCache& mosaics,std::wstring& error) {
    Image output=Crop(desktop,crop);
    if(!output.valid()){error=L"选区无效，请重新选择截图范围。";return {};}
    if(items.empty())return output;
    ComPtr<IWICImagingFactory> wic;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    ComPtr<IWICBitmap> bitmap;
    if(SUCCEEDED(hr))hr=wic->CreateBitmapFromMemory(output.width,output.height,GUID_WICPixelFormat32bppPBGRA,
        output.width*4,static_cast<UINT>(output.pixels.size()*4),reinterpret_cast<BYTE*>(output.pixels.data()),&bitmap);
    ComPtr<ID2D1RenderTarget> target;
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    if(SUCCEEDED(hr))hr=factory->CreateWicBitmapRenderTarget(bitmap.Get(),properties,&target);
    if(SUCCEEDED(hr)) {
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(crop.left),-static_cast<float>(crop.top)));
        hr=DrawAnnotations(target.Get(),textFactory,desktop,mosaics,items);
        const HRESULT end=target->EndDraw();if(SUCCEEDED(hr))hr=end;
    }
    target.Reset();
    if(SUCCEEDED(hr))hr=bitmap->CopyPixels(nullptr,output.width*4,static_cast<UINT>(output.pixels.size()*4),reinterpret_cast<BYTE*>(output.pixels.data()));
    if(FAILED(hr)){error=L"无法合成标注，请检查可用内存或重试。";return {};}
    for(auto& pixel:output.pixels)pixel|=0xff000000u;
    return output;
}
}
