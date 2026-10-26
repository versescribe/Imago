#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

namespace imago {
enum class ToolbarIcon {
    Select,Crop,Rectangle,Ellipse,Arrow,Text,Redact,Mosaic,Blur,Color,Picker,Undo,Redo,Scroll,Ocr,Record,Pin,Save,Copy,Cancel,
    Screenshot,Speaker,SpeakerMuted,Microphone,MicrophoneMuted,Resolution,FrameRate,StartRecording,Pause,Play,Stop
};
struct IconButtonStyle {
    COLORREF background{}, fill{}, foreground{}, border{}, focus{}, swatch{};
    bool dropdown{}, dropdownUp{}, focused{}, pressed{};
};

// One reusable DC target paints native owner-drawn buttons using resolution-independent paths.
class ToolbarRenderer {
public:
    bool Draw(HDC dc, const RECT& bounds, ToolbarIcon icon, const IconButtonStyle& style, ID2D1Factory* factory);
private:
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke_;
    void Reset();
    void Icon(ToolbarIcon icon, COLORREF swatch);
};
}
