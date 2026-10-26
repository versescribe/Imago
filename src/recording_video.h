#pragma once
#include "recording.h"
#include "recording_common.h"
#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <atomic>

namespace imago::recording {
class ScreenSource {
public:
    ScreenSource(const RecordingOptions& options,std::shared_ptr<Event> wake);
    ~ScreenSource();
    void Stop() noexcept;
    bool Update();
    ComPtr<IMFMediaBuffer> VideoBuffer();
private:
    SIZE input_{},output_{},monitorSize_{};
    RECT crop_{};
    bool haveFrame_{};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> cropped_,converted_,staging_;
    ComPtr<ID3D11VideoProcessorInputView> inputView_;
    ComPtr<ID3D11VideoProcessorOutputView> outputView_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker arrived_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem::Closed_revoker closed_;
    std::shared_ptr<std::atomic<bool>> targetClosed_{std::make_shared<std::atomic<bool>>(false)};
};
}
