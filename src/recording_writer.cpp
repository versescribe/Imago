#include "recording_writer.h"
#include <filesystem>
#include <cstring>

namespace imago::recording {
namespace {
ComPtr<IMFMediaType> VideoType(const RecordingOptions& options,const GUID& subtype) {
    ComPtr<IMFMediaType> type;Check(MFCreateMediaType(&type),L"无法创建视频格式。");
    Check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),L"无法设置视频类型。");
    Check(type->SetGUID(MF_MT_SUBTYPE,subtype),L"无法设置视频编码格式。");
    const auto size=RecordingSize(options);
    Check(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,size.cx,size.cy),L"无法设置视频分辨率。");
    Check(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,options.fps,1),L"无法设置视频帧率。");
    Check(MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),L"无法设置视频像素比例。");
    Check(type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),L"无法设置视频扫描格式。");
    type->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT709);
    type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235);
    return type;
}
}
VideoWriter::VideoWriter(const RecordingOptions& options,const std::wstring& path):destination_(path) {
    GUID guid{};Check(CoCreateGuid(&guid),L"无法创建录像临时文件。");
    wchar_t suffix[40]{};StringFromGUID2(guid,suffix,40);
    temporary_=(std::filesystem::path(path).parent_path()/(std::wstring(L".imago-record-")+suffix+L".mp4")).wstring();
    HANDLE file=CreateFileW(temporary_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"无法写入录像目录。");
    CloseHandle(file);owned_=true;
    try {
        try {Configure(options,true);}catch(const winrt::hresult_error&){writer_.Reset();Configure(options,false);}
    }catch(...) {writer_.Reset();DeleteFileW(temporary_.c_str());owned_=false;throw;}
}
VideoWriter::~VideoWriter(){writer_.Reset();if(owned_)DeleteFileW(temporary_.c_str());}
void VideoWriter::Configure(const RecordingOptions& options,bool hardware) {
    ComPtr<IMFAttributes> attributes;Check(MFCreateAttributes(&attributes,3),L"无法配置视频编码器。");
    attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,hardware?TRUE:FALSE);
    Check(MFCreateSinkWriterFromURL(temporary_.c_str(),nullptr,attributes.Get(),&writer_),L"无法创建 MP4 编码器，请检查 Windows 媒体功能。");
    auto output=VideoType(options,MFVideoFormat_H264);
    const auto size=RecordingSize(options);
    const auto rate=static_cast<UINT32>(std::clamp(static_cast<double>(size.cx)*size.cy*options.fps*.16,1500000.0,60000000.0));
    output->SetUINT32(MF_MT_AVG_BITRATE,rate);output->SetUINT32(MF_MT_MPEG2_PROFILE,100);
    Check(writer_->AddStream(output.Get(),&video_),L"无法创建 H.264 视频轨道。");
    auto input=VideoType(options,MFVideoFormat_NV12);
    Check(writer_->SetInputMediaType(video_,input.Get(),nullptr),L"编码器不支持该分辨率或帧率，请降低后重试。");
    if(options.systemAudio || options.microphone) {
        ComPtr<IMFMediaType> audio;Check(MFCreateMediaType(&audio),L"无法创建音频格式。");
        audio->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);audio->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC);
        audio->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,AudioChannels);audio->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,AudioRate);
        audio->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);audio->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,24000);
        Check(writer_->AddStream(audio.Get(),&audio_),L"无法创建 AAC 音频轨道。");
        ComPtr<IMFMediaType> pcm;Check(MFCreateMediaType(&pcm),L"无法创建音频输入格式。");
        Check(audio->CopyAllItems(pcm.Get()),L"无法配置音频输入格式。");
        pcm->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
        pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,AudioChannels*2);
        pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,AudioRate*AudioChannels*2);
        Check(writer_->SetInputMediaType(audio_,pcm.Get(),nullptr),L"无法初始化音频编码器。");
    }
    Check(writer_->BeginWriting(),L"无法开始编码，请尝试降低分辨率或帧率。");
}
void VideoWriter::Video(IMFMediaBuffer* buffer,LONGLONG time,LONGLONG duration) {
    auto sample=Sample(buffer,time,duration);
    Check(writer_->WriteSample(video_,sample.Get()),L"视频写入失败，请检查磁盘空间。");hasVideo_=true;
}
void VideoWriter::Audio(std::span<const std::int16_t> samples,LONGLONG firstFrame) {
    const DWORD bytes=static_cast<DWORD>(samples.size_bytes());
    ComPtr<IMFMediaBuffer> buffer;Check(MFCreateMemoryBuffer(bytes,&buffer),L"无法分配音频编码缓冲区。");
    {BufferLock locked(buffer.Get());std::memcpy(locked.data(),samples.data(),bytes);}
    buffer->SetCurrentLength(bytes);
    const LONGLONG count=static_cast<LONGLONG>(samples.size()/AudioChannels);
    auto sample=Sample(buffer.Get(),firstFrame*Second/AudioRate,count*Second/AudioRate);
    Check(writer_->WriteSample(audio_,sample.Get()),L"音频写入失败，请检查磁盘空间。");
}
void VideoWriter::Finish() {
    Check(writer_->Finalize(),L"无法完成 MP4 文件收尾。");writer_.Reset();
    if(!MoveFileExW(temporary_.c_str(),destination_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()),L"录像已生成，但无法替换目标文件。");
    owned_=false;
}
std::wstring VideoWriter::Preserve(){writer_.Reset();owned_=false;return temporary_;}
}
