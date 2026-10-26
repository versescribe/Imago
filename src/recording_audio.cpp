#include "recording_audio.h"
#include <mmdeviceapi.h>
#include <cstring>

namespace imago::recording {
AudioSource::AudioSource(bool loopback,const std::wstring& deviceId):name_(loopback?L"所选电脑音频设备不可用，请重新选择输出设备。":L"所选麦克风不可用，请重新选择设备，并检查 Windows 麦克风访问权限。") {
    ComPtr<IMMDeviceEnumerator> enumerator;
    Check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),name_);
    ComPtr<IMMDevice> device;
    if(deviceId.empty())Check(enumerator->GetDefaultAudioEndpoint(loopback?eRender:eCapture,eMultimedia,&device),name_);
    else Check(enumerator->GetDevice(deviceId.c_str(),&device),name_);
    DWORD state{};Check(device->GetState(&state),name_);
    Check((state&DEVICE_STATE_ACTIVE)?S_OK:E_FAIL,name_);
    ComPtr<IMMEndpoint> endpoint;Check(device.As(&endpoint),name_);
    EDataFlow flow{};Check(endpoint->GetDataFlow(&flow),name_);
    Check(flow==(loopback?eRender:eCapture)?S_OK:E_INVALIDARG,name_);
    Check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client_),name_);
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=AudioChannels;format.nSamplesPerSec=AudioRate;
    format.wBitsPerSample=16;format.nBlockAlign=AudioChannels*sizeof(std::int16_t);
    format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
    const DWORD flags=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY|
        (loopback?AUDCLNT_STREAMFLAGS_LOOPBACK:0);
    Check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,Second/2,0,&format,nullptr),name_);
    Check(client_->GetService(IID_PPV_ARGS(&capture_)),name_);
}
AudioSource::~AudioSource(){Stop();}
void AudioSource::Start(){Check(client_->Start(),name_);running_=true;}
void AudioSource::Stop() noexcept {if(running_){client_->Stop();running_=false;}}
void AudioSource::Suspend() {
    if(running_)Check(client_->Stop(),name_);
    running_=false;Check(client_->Reset(),name_);nextFrame_=-1;packets_.clear();
}
void AudioSource::Drain(LONGLONG origin,LONGLONG pausedTime,LONGLONG now) {
    UINT32 count{};
    for(;;) {
        Check(capture_->GetNextPacketSize(&count),name_);
        if(!count)break;
        BYTE* data{};DWORD flags{};UINT64 devicePosition{},qpc{};
        Check(capture_->GetBuffer(&data,&count,&flags,&devicePosition,&qpc),name_);
        struct Release {IAudioCaptureClient* client;UINT32 count;~Release(){client->ReleaseBuffer(count);}} release{capture_.Get(),count};
        if(!count)break;
        const bool badTime=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)!=0 || !qpc;
        LONGLONG first=badTime && nextFrame_>=0?nextFrame_:
            ((badTime?now-static_cast<LONGLONG>(count)*Second/AudioRate:static_cast<LONGLONG>(qpc))-origin-pausedTime)*AudioRate/Second;
        // Correct only sub-millisecond timestamp jitter; preserve real gaps as silence.
        if(nextFrame_>=0 && std::abs(first-nextFrame_)<AudioRate/1000)first=nextFrame_;
        nextFrame_=first+count;
        Packet packet{first,std::vector<std::int16_t>(static_cast<std::size_t>(count)*AudioChannels)};
        if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT) && data)std::memcpy(packet.samples.data(),data,packet.samples.size()*sizeof(std::int16_t));
        packets_.push_back(std::move(packet));
        // Bound application-owned audio to two seconds even if an encoder stalls.
        while(!packets_.empty() && nextFrame_-packets_.front().first>AudioRate*2)packets_.pop_front();
    }
}
void AudioSource::Mix(LONGLONG firstFrame,std::span<int> destination) {
    const LONGLONG endFrame=firstFrame+static_cast<LONGLONG>(destination.size()/AudioChannels);
    for(const auto& packet:packets_) {
        const LONGLONG end=packet.first+static_cast<LONGLONG>(packet.samples.size()/AudioChannels);
        const LONGLONG left=std::max(firstFrame,packet.first),right=std::min(endFrame,end);
        if(right<=left)continue;
        for(LONGLONG frame=left;frame<right;++frame)for(int channel=0;channel<AudioChannels;++channel)
            destination[static_cast<std::size_t>(frame-firstFrame)*AudioChannels+channel]+=
                packet.samples[static_cast<std::size_t>(frame-packet.first)*AudioChannels+channel];
    }
    while(!packets_.empty() && packets_.front().first+static_cast<LONGLONG>(packets_.front().samples.size()/AudioChannels)<=endFrame)
        packets_.pop_front();
}
}
