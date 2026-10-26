#include "recording_video.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11_1.h>
#include <cstring>

namespace imago::recording {
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

ScreenSource::ScreenSource(const RecordingOptions& options,std::shared_ptr<Event> wake) {
    Check(GraphicsCaptureSession::IsSupported()?S_OK:E_NOTIMPL,L"当前系统或显卡不支持 Windows 录屏接口。");
    const auto monitor=MonitorFromRect(&options.region,MONITOR_DEFAULTTONULL);
    MONITORINFO info{sizeof(info)};
    if(!monitor || !GetMonitorInfoW(monitor,&info))throw winrt::hresult_error(E_FAIL,L"找不到录屏选区所在的显示器。");
    crop_=options.region;OffsetRect(&crop_,-info.rcMonitor.left,-info.rcMonitor.top);
    input_={crop_.right-crop_.left,crop_.bottom-crop_.top};output_=RecordingSize(options);
    const UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,
        &device_,nullptr,&context_),L"无法初始化显卡录屏设备，请检查显卡驱动。");
    Check(device_.As(&videoDevice_),L"显卡不支持视频处理。");
    Check(context_.As(&videoContext_),L"无法创建视频处理上下文。");
    auto factory=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
    Check(factory->CreateForMonitor(monitor,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(item_)),L"无法采集所选显示器。");
    monitorSize_={item_.Size().Width,item_.Size().Height};
    ComPtr<IDXGIDevice> dxgi;Check(device_.As(&dxgi),L"无法创建屏幕采集设备。");
    winrt::com_ptr<IInspectable> inspectable;
    Check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspectable.put()),L"无法连接系统屏幕采集接口。");
    auto direct3d=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    pool_=Direct3D11CaptureFramePool::CreateFreeThreaded(direct3d,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item_.Size());
    session_=pool_.CreateCaptureSession(item_);
    session_.IsCursorCaptureEnabled(true);
    arrived_=pool_.FrameArrived(winrt::auto_revoke,[wake](auto const&,auto const&){SetEvent(wake->handle);});
    closed_=item_.Closed(winrt::auto_revoke,[closed=targetClosed_,wake](auto const&,auto const&){closed->store(true);SetEvent(wake->handle);});

    D3D11_TEXTURE2D_DESC texture{};
    texture.Width=input_.cx;texture.Height=input_.cy;texture.MipLevels=1;texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
    texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Check(device_->CreateTexture2D(&texture,nullptr,&cropped_),L"无法创建录屏裁剪缓冲区。");
    texture.Width=output_.cx;texture.Height=output_.cy;texture.Format=DXGI_FORMAT_NV12;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
    Check(device_->CreateTexture2D(&texture,nullptr,&converted_),L"显卡不支持所选录屏分辨率，请降低分辨率。");
    texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(device_->CreateTexture2D(&texture,nullptr,&staging_),L"无法创建视频编码缓冲区。");
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC description{};
    description.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    description.InputFrameRate={static_cast<UINT>(options.fps),1};description.OutputFrameRate=description.InputFrameRate;
    description.InputWidth=input_.cx;description.InputHeight=input_.cy;
    description.OutputWidth=output_.cx;description.OutputHeight=output_.cy;description.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    Check(videoDevice_->CreateVideoProcessorEnumerator(&description,&enumerator_),L"无法创建录屏缩放器。");
    Check(videoDevice_->CreateVideoProcessor(enumerator_.Get(),0,&processor_),L"无法初始化录屏颜色转换。");
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputView{};inputView.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
    Check(videoDevice_->CreateVideoProcessorInputView(cropped_.Get(),enumerator_.Get(),&inputView,&inputView_),L"无法创建视频输入。");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputView{};outputView.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
    Check(videoDevice_->CreateVideoProcessorOutputView(converted_.Get(),enumerator_.Get(),&outputView,&outputView_),L"无法创建视频输出。");
    RECT source{0,0,input_.cx,input_.cy},destination{0,0,output_.cx,output_.cy};
    videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(),0,TRUE,&source);
    videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(),0,TRUE,&destination);
    videoContext_->VideoProcessorSetOutputTargetRect(processor_.Get(),TRUE,&destination);
    videoContext_->VideoProcessorSetStreamFrameFormat(processor_.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(),0,FALSE);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{},yuv{};
    rgb.RGB_Range=0;rgb.Nominal_Range=D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    yuv.YCbCr_Matrix=1;yuv.Nominal_Range=D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    videoContext_->VideoProcessorSetStreamColorSpace(processor_.Get(),0,&rgb);
    videoContext_->VideoProcessorSetOutputColorSpace(processor_.Get(),&yuv);
    session_.StartCapture();
}
ScreenSource::~ScreenSource(){Stop();}
void ScreenSource::Stop() noexcept {
    arrived_.revoke();closed_.revoke();
    try {if(session_)session_.Close();}catch(...){}
    try {if(pool_)pool_.Close();}catch(...){}
    session_=nullptr;pool_=nullptr;
}
bool ScreenSource::Update() {
    if(targetClosed_->load())throw winrt::hresult_error(E_FAIL,L"录屏显示器已断开。");
    for(int i=0;i<2;++i) {
        auto frame=pool_.TryGetNextFrame();if(!frame)break;
        const auto size=frame.ContentSize();
        if(size.Width!=monitorSize_.cx || size.Height!=monitorSize_.cy || crop_.right>size.Width || crop_.bottom>size.Height)
            throw winrt::hresult_error(E_FAIL,L"显示器分辨率已变化，录屏已停止。");
        auto access=frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        Check(access->GetInterface(IID_PPV_ARGS(&texture)),L"无法读取系统采集画面。");
        D3D11_BOX box{static_cast<UINT>(crop_.left),static_cast<UINT>(crop_.top),0,
            static_cast<UINT>(crop_.right),static_cast<UINT>(crop_.bottom),1};
        context_->CopySubresourceRegion(cropped_.Get(),0,0,0,0,texture.Get(),0,&box);
        haveFrame_=true;frame.Close();
    }
    Check(device_->GetDeviceRemovedReason(),L"显卡设备发生变化，录屏已停止。");
    return haveFrame_;
}
ComPtr<IMFMediaBuffer> ScreenSource::VideoBuffer() {
    D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=inputView_.Get();
    Check(videoContext_->VideoProcessorBlt(processor_.Get(),outputView_.Get(),0,1,&stream),L"视频裁剪或缩放失败。");
    context_->CopyResource(staging_.Get(),converted_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context_->Map(staging_.Get(),0,D3D11_MAP_READ,0,&mapped),L"无法读取编码画面。");
    struct Unmap {ID3D11DeviceContext* context;ID3D11Texture2D* texture;~Unmap(){context->Unmap(texture,0);}} unmap{context_.Get(),staging_.Get()};
    const DWORD bytes=static_cast<DWORD>(output_.cx*output_.cy*3/2);
    ComPtr<IMFMediaBuffer> buffer;Check(MFCreateMemoryBuffer(bytes,&buffer),L"无法分配视频编码缓冲区。");
    {
        BufferLock locked(buffer.Get());
        for(int row=0;row<output_.cy*3/2;++row)std::memcpy(locked.data()+static_cast<std::size_t>(row)*output_.cx,
            static_cast<const BYTE*>(mapped.pData)+static_cast<std::size_t>(row)*mapped.RowPitch,output_.cx);
    }
    Check(buffer->SetCurrentLength(bytes),L"无法设置视频数据长度。");
    return buffer;
}
}
