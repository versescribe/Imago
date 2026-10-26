#pragma once
#include "recording.h"
#include "appearance.h"
#include <d2d1.h>

namespace imago {
enum class RecordingAction { Back, Reselect, Start, Pause, Stop, Copy, Save, Preview, Close };
class RecordingPanel {
public:
    RecordingPanel();
    ~RecordingPanel();
    bool Create(HINSTANCE instance,HWND owner,UINT message,ID2D1Factory* factory,const Palette& palette,bool animations);
    void Region(RECT region);
    void Show(bool show);
    void Active(const RecordingStatus& status);
    void Result(const RecordingStatus& status);
    void Exporting(int percent);
    void Theme(const Palette& palette,bool animations);
    bool DialogMessage(MSG& message);
    RecordingOptions Options() const;
    HWND Window() const;
    bool ExcludedFromCapture() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
