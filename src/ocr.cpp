#include "ocr.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace imago {
namespace {
using namespace winrt::Windows::Graphics::Imaging;
using winrt::Windows::Media::Ocr::OcrEngine;
struct Event {
    HANDLE handle{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Event(){if(!handle)winrt::throw_last_error();}
    ~Event(){CloseHandle(handle);}
};
struct Apartment {
    Apartment(){winrt::init_apartment(winrt::apartment_type::multi_threaded);}
    ~Apartment(){winrt::uninit_apartment();}
};
SoftwareBitmap MakeBitmap(const Image& image,int maximum,std::stop_token stop,bool& resized) {
    const double ratio=std::min({1.0,static_cast<double>(maximum)/image.width,static_cast<double>(maximum)/image.height});
    const int width=std::max(1,static_cast<int>(std::floor(image.width*ratio)));
    const int height=std::max(1,static_cast<int>(std::floor(image.height*ratio)));
    const auto bytes=static_cast<UINT32>(static_cast<std::size_t>(width)*height*4);
    winrt::Windows::Storage::Streams::Buffer buffer(bytes);buffer.Length(bytes);
    auto* destination=reinterpret_cast<std::uint32_t*>(buffer.data());
    resized=width!=image.width || height!=image.height;
    if(!resized)std::memcpy(destination,image.pixels.data(),bytes);
    else {
        // Bound memory to the OCR engine's limit; resample directly into its input buffer.
        for(int y=0;y<height;++y) {
            if(stop.stop_requested())return nullptr;
            const double sy=std::clamp((y+.5)*image.height/height-.5,0.0,static_cast<double>(image.height-1));
            const int y0=static_cast<int>(sy),y1=std::min(y0+1,image.height-1);const double fy=sy-y0;
            for(int x=0;x<width;++x) {
                const double sx=std::clamp((x+.5)*image.width/width-.5,0.0,static_cast<double>(image.width-1));
                const int x0=static_cast<int>(sx),x1=std::min(x0+1,image.width-1);const double fx=sx-x0;
                const auto a=image.pixels[static_cast<std::size_t>(y0)*image.width+x0];
                const auto b=image.pixels[static_cast<std::size_t>(y0)*image.width+x1];
                const auto c=image.pixels[static_cast<std::size_t>(y1)*image.width+x0];
                const auto d=image.pixels[static_cast<std::size_t>(y1)*image.width+x1];
                std::uint32_t pixel=0xff000000u;
                for(int shift:{0,8,16}) {
                    const double top=((a>>shift)&255)*(1-fx)+((b>>shift)&255)*fx;
                    const double bottom=((c>>shift)&255)*(1-fx)+((d>>shift)&255)*fx;
                    pixel|=static_cast<std::uint32_t>(std::lround(top*(1-fy)+bottom*fy))<<shift;
                }
                destination[static_cast<std::size_t>(y)*width+x]=pixel;
            }
        }
    }
    if(stop.stop_requested())return nullptr;
    SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8,width,height,BitmapAlphaMode::Ignore);
    bitmap.CopyFromBuffer(buffer);return bitmap;
}
OcrResult Recognize(Image image,std::stop_token stop) noexcept {
    OcrResult result;
    try {
        Apartment apartment;
        if(stop.stop_requested()){result.status=OcrStatus::Cancelled;return result;}
        auto engine=OcrEngine::TryCreateFromUserProfileLanguages();
        if(!engine)for(const auto& language:OcrEngine::AvailableRecognizerLanguages()) {
            engine=OcrEngine::TryCreateFromLanguage(language);if(engine)break;
        }
        if(!engine) {
            result.status=OcrStatus::NoLanguage;
            result.error=L"没有可用的 Windows OCR 语言包。请在 Windows「语言和区域」中安装所需语言及 OCR 功能后重试。";
            return result;
        }
        result.language=engine.RecognizerLanguage().DisplayName().c_str();
        const auto maximum=OcrEngine::MaxImageDimension();
        if(!maximum || maximum>INT_MAX)winrt::throw_hresult(E_UNEXPECTED);
        auto bitmap=MakeBitmap(image,static_cast<int>(maximum),stop,result.resized);
        image={}; // The input buffer is owned by SoftwareBitmap from this point on.
        if(!bitmap || stop.stop_requested()){result.status=OcrStatus::Cancelled;return result;}
        auto complete=std::make_shared<Event>();Event cancel;
        std::stop_callback onStop(stop,[&cancel]{SetEvent(cancel.handle);});
        auto operation=engine.RecognizeAsync(bitmap);
        operation.Completed([complete](const auto&,auto) noexcept {SetEvent(complete->handle);});
        const HANDLE events[]{cancel.handle,complete->handle};
        const DWORD ready=WaitForMultipleObjects(2,events,FALSE,60000);
        if(stop.stop_requested() || ready==WAIT_OBJECT_0) {
            operation.Cancel();result.status=OcrStatus::Cancelled;return result;
        }
        if(ready!=WAIT_OBJECT_0+1) {
            operation.Cancel();result.error=L"文字识别超时或系统未响应，请缩小选区后重试。";return result;
        }
        const auto recognized=operation.GetResults();
        for(const auto& line:recognized.Lines()) {
            const auto text=line.Text();
            if(text.empty())continue;
            if(!result.text.empty())result.text+=L"\r\n";
            result.text.append(text.c_str(),text.size());
        }
        if(result.text.find_first_not_of(L" \t\r\n\u3000")==std::wstring::npos) {
            result.text.clear();result.status=OcrStatus::NoText;
            result.error=L"未识别到文字，请调整选区或放大原始内容后重试。";
        }else result.status=OcrStatus::Success;
    }catch(const winrt::hresult_error& exception) {
        if(stop.stop_requested()){result.status=OcrStatus::Cancelled;return result;}
        const HRESULT code=exception.code();
        result.status=code==REGDB_E_CLASSNOTREG || code==E_NOINTERFACE?OcrStatus::Unavailable:OcrStatus::Failed;
        try {
            wchar_t hex[16]{};swprintf_s(hex,L"0x%08X",static_cast<unsigned>(code));
            result.error=L"Windows 文字识别不可用或未能完成（"+std::wstring(hex)+L"）。请检查本机 OCR 语言功能后重试。";
        }catch(...) {}
    }catch(...) {
        result.status=stop.stop_requested()?OcrStatus::Cancelled:OcrStatus::Failed;
    }
    return result;
}
}
struct OcrTask::Impl {
    std::mutex mutex;
    std::optional<OcrResult> result;
    std::jthread worker;
    WPARAM request{};
    HWND window{};
    UINT message{};
};
OcrTask::OcrTask():impl_(std::make_unique<Impl>()){}
OcrTask::~OcrTask(){Cancel();}
void OcrTask::Cancel() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->window=nullptr;impl_->result.reset();++impl_->request;
    }
    if(impl_->worker.joinable()){impl_->worker.request_stop();impl_->worker.join();}
}
bool OcrTask::Start(Image image,HWND window,UINT message,std::wstring& error) {
    Cancel();
    if(!image.valid()){error=L"没有可识别的图像。";return false;}
    const auto request=impl_->request;impl_->window=window;impl_->message=message;
    try {
        impl_->worker=std::jthread([state=impl_.get(),request,image=std::move(image)](std::stop_token stop) mutable {
            auto result=Recognize(std::move(image),stop);
            std::lock_guard lock(state->mutex);
            if(stop.stop_requested() || !state->window || request!=state->request)return;
            state->result=std::move(result);
            PostMessageW(state->window,state->message,request,0);
        });
    }catch(...) {impl_->window=nullptr;error=L"无法启动文字识别，请检查可用系统资源。";return false;}
    return true;
}
std::optional<OcrResult> OcrTask::TakeResult(WPARAM request) {
    std::lock_guard lock(impl_->mutex);
    if(request!=impl_->request || !impl_->result)return {};
    auto result=std::move(impl_->result);impl_->result.reset();return result;
}
}
