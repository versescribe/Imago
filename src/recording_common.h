#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace imago::recording {
using Microsoft::WRL::ComPtr;
constexpr LONGLONG Second=10000000;
constexpr int AudioRate=48000,AudioChannels=2,AudioBlock=480;
inline void Check(HRESULT hr,const wchar_t* message) {
    if(FAILED(hr))throw winrt::hresult_error(hr,message);
}
inline LONGLONG Clock() {
    LARGE_INTEGER counter{},frequency{};
    QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);
    return counter.QuadPart/frequency.QuadPart*Second+
        counter.QuadPart%frequency.QuadPart*Second/frequency.QuadPart;
}
struct Event {
    HANDLE handle{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    Event(){if(!handle)winrt::throw_last_error();}
    ~Event(){CloseHandle(handle);}
    Event(const Event&)=delete;
    Event& operator=(const Event&)=delete;
};
class BufferLock {
public:
    explicit BufferLock(IMFMediaBuffer* buffer):buffer_(buffer) {
        Check(buffer_->Lock(&data_,nullptr,nullptr),L"无法访问编码缓冲区。");
    }
    ~BufferLock(){buffer_->Unlock();}
    BYTE* data() const{return data_;}
    BufferLock(const BufferLock&)=delete;
    BufferLock& operator=(const BufferLock&)=delete;
private:
    IMFMediaBuffer* buffer_;
    BYTE* data_{};
};
inline ComPtr<IMFSample> Sample(IMFMediaBuffer* buffer,LONGLONG time,LONGLONG duration) {
    ComPtr<IMFSample> sample;
    Check(MFCreateSample(&sample),L"无法创建编码样本。");
    Check(sample->AddBuffer(buffer),L"无法添加编码样本。");
    Check(sample->SetSampleTime(time),L"无法设置录像时间。");
    Check(sample->SetSampleDuration(duration),L"无法设置录像时长。");
    return sample;
}
}
