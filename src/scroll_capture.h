#pragma once
#include "image.h"

namespace imago {
constexpr int MaxScrollHeight=60000;
constexpr std::size_t MaxScrollBytes=128u*1024u*1024u;
bool FramesStable(const Image& a,const Image& b);
enum class StitchResult { Appended, Revisited, Unchanged, Uncertain, Limit };
class ScrollStitcher {
public:
    void Begin(Image frame);
    StitchResult Append(Image frame);
    int Height() const { return output_.height; }
    int Frames() const { return frames_; }
    int Position() const { return position_; }
    int ViewportHeight() const { return previous_.height; }
    const Image& Result() const { return output_; }
    Image Take() { previous_={};return std::move(output_); }
private:
    Image previous_,output_;
    int frames_{},position_{};
};
}
