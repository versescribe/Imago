#pragma once
#include "appearance.h"
#include <memory>

namespace imago {
class RecordingBorder {
public:
    RecordingBorder();
    ~RecordingBorder();
    bool Create(HINSTANCE instance,HWND owner,RECT region,const Palette& palette);
    void Show();
    void Appearance(const Palette& palette,bool paused);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
