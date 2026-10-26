#pragma once
#include "recording_common.h"
#include <audioclient.h>
#include <deque>
#include <span>

namespace imago::recording {
// All WASAPI calls remain on the recorder worker. Packets retain their QPC time
// until mixed, so separate devices do not drift merely because polling is late.
class AudioSource {
public:
    AudioSource(bool loopback,const std::wstring& deviceId);
    ~AudioSource();
    void Start();
    void Stop() noexcept;
    void Suspend();
    void Drain(LONGLONG origin,LONGLONG pausedTime,LONGLONG now);
    void Mix(LONGLONG firstFrame,std::span<int> destination);
private:
    struct Packet {LONGLONG first{};std::vector<std::int16_t> samples;};
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    std::deque<Packet> packets_;
    bool running_{};
    const wchar_t* name_;
    LONGLONG nextFrame_{-1};
};
}
