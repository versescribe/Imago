#pragma once
#include "scroll_capture.h"

namespace imago {
enum class ScrollMode { Manual, Up, Down };
enum class ScrollPhase { Initial, Wheel, Settle };
enum class ScrollPause { None, NoMovement, Unstable, Unaligned, TargetFocus, CursorPosition };
enum class ScrollUpdate { None, Status, Image, Limit };

// Owns capture transitions and verified images. Window activation, input injection,
// rendering and status text belong to the UI; all times are supplied by the caller.
class ScrollController {
public:
    void Start(ULONGLONG now);
    void SelectMode(ScrollMode mode, ULONGLONG now);
    void ManualWheel(int delta, ULONGLONG now);
    void Pause(ScrollPause reason, ULONGLONG now);
    void WaitForInput(ULONGLONG now) { phaseTime_ = now; }
    void WheelSent(ULONGLONG now);
    ScrollUpdate ObserveFrame(Image frame, ULONGLONG now);

    ScrollMode Mode() const { return mode_; }
    ScrollPhase Phase() const { return phase_; }
    ScrollPause PauseReason() const { return pause_; }
    bool Automatic() const { return mode_ != ScrollMode::Manual; }
    int WheelDelta() const;
    bool FrameDue(ULONGLONG now) const;
    bool WheelDue(ULONGLONG now) const;
    bool TimeLimitReached(ULONGLONG now) const;
    bool StepLimitReached() const;
    const ScrollStitcher& Stitch() const { return stitch_; }
    Image TakeImage() { return stitch_.Take(); }

private:
    bool AlignmentPaused() const;
    void ResetProbe(ULONGLONG now);
    void Advance(ULONGLONG now);

    ScrollMode mode_{ScrollMode::Down};
    ScrollPhase phase_{ScrollPhase::Initial};
    ScrollPause pause_{ScrollPause::None};
    ScrollMode lastWheel_{ScrollMode::Manual};
    std::optional<ScrollMode> pendingWheel_;
    ULONGLONG began_{}, phaseTime_{}, lastSample_{};
    int steady_{}, unchanged_{}, steps_{}, uncertain_{};
    Image probe_;
    ScrollStitcher stitch_;
};
}
