#include "scroll_controller.h"

namespace imago {
namespace {
constexpr ULONGLONG InitialDelay = 500;
constexpr ULONGLONG WheelInterval = 300;
constexpr ULONGLONG SettleDelay = 220;
constexpr ULONGLONG SampleInterval = 140;
constexpr ULONGLONG RecoveryInterval = 400;
constexpr ULONGLONG SettleTimeout = 3500;
constexpr ULONGLONG CaptureTimeout = 300000;
constexpr int MaximumSteps = 120;
constexpr int StableSamples = 2;
constexpr int UnchangedLimit = 3;
constexpr int AutomaticRetries = 5;
constexpr int ManualRetries = 10;
}

void ScrollController::Start(ULONGLONG now) {
    *this = ScrollController{};
    began_ = phaseTime_ = lastSample_ = now;
}

bool ScrollController::AlignmentPaused() const {
    return pause_ == ScrollPause::Unaligned || pause_ == ScrollPause::Unstable;
}

void ScrollController::ResetProbe(ULONGLONG now) {
    probe_ = {};
    steady_ = 0;
    uncertain_ = 0;
    phaseTime_ = now;
}

void ScrollController::SelectMode(ScrollMode mode, ULONGLONG now) {
    const bool reverseRecovery = AlignmentPaused() && lastWheel_ != ScrollMode::Manual &&
        mode != ScrollMode::Manual && mode != lastWheel_;
    const bool needsSettle = phase_ == ScrollPhase::Settle || pendingWheel_ || !Automatic();
    mode_ = mode;
    pause_ = ScrollPause::None;
    unchanged_ = 0;
    ResetProbe(now);
    if (phase_ != ScrollPhase::Initial) {
        phase_ = !Automatic() || (needsSettle && !reverseRecovery)
            ? ScrollPhase::Settle : ScrollPhase::Wheel;
    }
}

void ScrollController::ManualWheel(int delta, ULONGLONG now) {
    mode_ = ScrollMode::Manual;
    pause_ = ScrollPause::None;
    pendingWheel_.reset();
    unchanged_ = 0;
    if (delta) lastWheel_ = delta > 0 ? ScrollMode::Up : ScrollMode::Down;
    ResetProbe(now);
    if (phase_ != ScrollPhase::Initial) phase_ = ScrollPhase::Settle;
}

void ScrollController::Pause(ScrollPause reason, ULONGLONG now) {
    mode_ = ScrollMode::Manual;
    pause_ = reason;
    pendingWheel_.reset();
    ResetProbe(now);
    lastSample_ = now;
    // Activation can fail before the first frame. Recovery must still initialize
    // the stitcher before attempting to align subsequent frames.
    phase_ = stitch_.Frames() ? ScrollPhase::Settle : ScrollPhase::Initial;
}

int ScrollController::WheelDelta() const {
    switch (mode_) {
    case ScrollMode::Up: return WHEEL_DELTA;
    case ScrollMode::Down: return -WHEEL_DELTA;
    case ScrollMode::Manual: return 0;
    }
    return 0;
}

bool ScrollController::FrameDue(ULONGLONG now) const {
    if (phase_ == ScrollPhase::Initial) return now - phaseTime_ >= InitialDelay;
    if (phase_ != ScrollPhase::Settle) return false;
    return (!Automatic() || now - phaseTime_ >= SettleDelay) &&
        now - lastSample_ >= (AlignmentPaused() ? RecoveryInterval : SampleInterval);
}

bool ScrollController::WheelDue(ULONGLONG now) const {
    return phase_ == ScrollPhase::Wheel && Automatic() && now - phaseTime_ >= WheelInterval;
}

bool ScrollController::TimeLimitReached(ULONGLONG now) const { return now - began_ > CaptureTimeout; }
bool ScrollController::StepLimitReached() const { return steps_ >= MaximumSteps; }

void ScrollController::WheelSent(ULONGLONG now) {
    if (phase_ != ScrollPhase::Wheel || !Automatic()) return;
    ++steps_;
    lastWheel_ = mode_;
    pendingWheel_ = mode_;
    phase_ = ScrollPhase::Settle;
    ResetProbe(now);
}

void ScrollController::Advance(ULONGLONG now) {
    pendingWheel_.reset();
    ResetProbe(now);
    phase_ = Automatic() ? ScrollPhase::Wheel : ScrollPhase::Settle;
}

ScrollUpdate ScrollController::ObserveFrame(Image frame, ULONGLONG now) {
    if (!frame.valid() || phase_ == ScrollPhase::Wheel) return ScrollUpdate::None;
    lastSample_ = now;
    if (phase_ == ScrollPhase::Initial) {
        stitch_.Begin(std::move(frame));
        pause_ = ScrollPause::None;
        Advance(now);
        return ScrollUpdate::Image;
    }

    if (Automatic()) {
        steady_ = probe_.valid() && FramesStable(probe_, frame) ? steady_ + 1 : 0;
        if (steady_ < StableSamples) {
            probe_ = std::move(frame);
            if (now - phaseTime_ > SettleTimeout) {
                Pause(ScrollPause::Unstable, now);
                return ScrollUpdate::Status;
            }
            return ScrollUpdate::None;
        }
    }

    const auto result = stitch_.Append(std::move(frame));
    if (result == StitchResult::Uncertain) {
        // Keep the last verified baseline and retry this viewport without another wheel.
        if (!AlignmentPaused() && ++uncertain_ >= (Automatic() ? AutomaticRetries : ManualRetries)) {
            Pause(ScrollPause::Unaligned, now);
            return ScrollUpdate::Status;
        }
        return ScrollUpdate::None;
    }
    if (result == StitchResult::Limit) return ScrollUpdate::Limit;

    const bool recovered = pause_ != ScrollPause::None && pause_ != ScrollPause::NoMovement;
    if (recovered) pause_ = ScrollPause::None;
    if (result == StitchResult::Unchanged && pendingWheel_ && *pendingWheel_ == mode_) {
        if (++unchanged_ >= UnchangedLimit) {
            Pause(ScrollPause::NoMovement, now);
            return ScrollUpdate::Status;
        }
    } else if (result != StitchResult::Unchanged) {
        unchanged_ = 0;
        pause_ = ScrollPause::None;
    }
    Advance(now);
    if (result == StitchResult::Appended) return ScrollUpdate::Image;
    return result == StitchResult::Revisited || recovered ? ScrollUpdate::Status : ScrollUpdate::None;
}
}
