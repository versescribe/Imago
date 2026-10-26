#pragma once
#include "recording.h"
#include "recording_common.h"
#include <mfreadwrite.h>
#include <span>

namespace imago::recording {
class VideoWriter {
public:
    VideoWriter(const RecordingOptions& options,const std::wstring& path);
    ~VideoWriter();
    void Video(IMFMediaBuffer* buffer,LONGLONG time,LONGLONG duration);
    void Audio(std::span<const std::int16_t> samples,LONGLONG firstFrame);
    void Finish();
    std::wstring Preserve();
    bool HasVideo() const{return hasVideo_;}
private:
    void Configure(const RecordingOptions& options,bool hardware);
    std::wstring destination_,temporary_;
    bool owned_{},hasVideo_{};
    DWORD video_{},audio_{};
    ComPtr<IMFSinkWriter> writer_;
};
}
