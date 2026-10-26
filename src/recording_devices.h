#pragma once
#include <string>
#include <vector>

namespace imago {
struct RecordingAudioDevice {std::wstring id,name;};
struct RecordingAudioDevices {
    std::vector<RecordingAudioDevice> devices;
    std::wstring defaultId,defaultName,error;
};
// Enumerates endpoint metadata only. No audio client is activated or opened here.
RecordingAudioDevices ListRecordingAudioDevices(bool loopback);
}
