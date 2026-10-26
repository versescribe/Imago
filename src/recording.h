#pragma once
#include <windows.h>
#include <memory>
#include <string>

namespace imago {
struct RecordingOptions {
    RECT region{}; // Physical desktop pixels; one monitor per recording.
    int maxHeight{}; // 0 = original; otherwise fit inside a 16:9 resolution envelope.
    int fps{30};
    bool systemAudio{};
    bool microphone{};
    // Empty IDs follow the default endpoint at start; explicit IDs never silently fall back.
    std::wstring systemAudioDevice,microphoneDevice;
};
enum class RecordingState { Idle, Starting, Recording, Paused, Finalizing, Completed, Cancelled, Failed };
struct RecordingStatus {
    RecordingState state{RecordingState::Idle};
    LONGLONG elapsed{}; // 100 ns, excluding pauses.
    std::wstring path, error;
};
SIZE RecordingSize(const RecordingOptions& options);
bool ValidateRecording(const RecordingOptions& options,std::wstring& error);

class Recorder {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&)=delete;
    Recorder& operator=(const Recorder&)=delete;
    bool Start(const RecordingOptions& options,const std::wstring& path,std::wstring& error);
    void SetPaused(bool paused);
    void Stop();
    void Join();
    RecordingStatus Status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
