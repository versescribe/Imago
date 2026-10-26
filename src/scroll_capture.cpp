#include "scroll_capture.h"
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace imago {
namespace {
constexpr int Samples=64;
struct Row {std::array<int,Samples> value{};int texture{};};
std::vector<Row> Describe(const Image& image) {
    std::vector<Row> rows(image.height);
    // Ignore scrollbars and border shadows at either side of the chosen content region.
    const int margin=std::max(4,image.width/20);
    for(int y=0;y<image.height;++y)for(int sample=0;sample<Samples;++sample) {
        const int x=margin+(image.width-2*margin-1)*sample/(Samples-1);
        auto pixel=image.pixels[static_cast<std::size_t>(y)*image.width+x];
        auto& row=rows[y];row.value[sample]=static_cast<int>((((pixel>>16)&255)*77+((pixel>>8)&255)*150+(pixel&255)*29)>>8);
        if(sample)row.texture+=std::abs(row.value[sample]-row.value[sample-1]);
    }
    return rows;
}
float RowError(const Row& a,const Row& b) {
    if(a.texture<80 && b.texture<80)return -1;
    int error=0,count=0;
    for(int i=1;i<Samples;++i) {
        if(std::abs(a.value[i]-a.value[i-1])<12 && std::abs(b.value[i]-b.value[i-1])<12)continue;
        error+=std::abs(a.value[i]-b.value[i])+std::abs(a.value[i-1]-b.value[i-1]);count+=2;
    }
    return count>=4?static_cast<float>(error)/count:-1.f;
}
struct Match {int shift{};float score{1000};int support{};};
Match Score(const std::vector<Row>& a,const std::vector<Row>& b,int shift,int sampleCount) {
    const int height=static_cast<int>(a.size()),margin=std::min(64,height/10);
    const int start=std::max(0,-shift)+margin,end=std::min(height,height-shift)-margin;
    if(end-start<48)return {};
    std::vector<float> errors;errors.reserve(sampleCount+1);
    const int step=std::max(1,(end-start)/sampleCount);
    int supported=0;
    for(int y=start;y<end;y+=step) {
        const auto error=RowError(a[y+shift],b[y]);
        if(error<0)continue;
        errors.push_back(error);if(error<=5)++supported;
    }
    if(errors.size()<static_cast<std::size_t>(sampleCount<40?4:8))return {};
    std::sort(errors.begin(),errors.end());
    const std::size_t keep=std::max<std::size_t>(1,errors.size()*3/4);
    float sum=0;for(std::size_t i=0;i<keep;++i)sum+=errors[i];
    const float support=static_cast<float>(supported)/errors.size();
    return {shift,sum/keep+(1-support)*10,support>=.65f?supported:0};
}
std::optional<int> MatchShift(const Image& previous,const Image& next,int& seam) {
    const auto a=Describe(previous),b=Describe(next);
    std::vector<Match> candidates;
    // At least 25% overlap is required, even when the system uses page-sized wheel steps.
    for(int shift=-previous.height*3/4;shift<=previous.height*3/4;++shift) {
        const auto match=Score(a,b,shift,28);
        if(match.score<1000)candidates.push_back(match);
    }
    if(candidates.empty())return std::nullopt;
    std::sort(candidates.begin(),candidates.end(),[](const auto& left,const auto& right){return left.score<right.score;});
    if(candidates.size()>16)candidates.resize(16);
    for(auto& candidate:candidates)candidate=Score(a,b,candidate.shift,128);
    std::sort(candidates.begin(),candidates.end(),[](const auto& left,const auto& right){return left.score<right.score;});
    const auto best=candidates.front();
    if(!best.support || best.score>6)return std::nullopt;
    // Repeated identical rows cannot establish a trustworthy displacement.
    for(const auto& other:candidates)if(std::abs(other.shift-best.shift)>3 && other.score<=best.score+.7f)return std::nullopt;
    // Moving back from the direction buttons can change hover highlights without scrolling.
    // Zero displacement is a valid match, distinct from an alignment failure.
    if(!best.shift)return 0;
    const int start=std::max(0,-best.shift),overlap=previous.height-std::abs(best.shift);
    float seamError=1000;seam=-1;
    for(int y=start+std::max(12,overlap/4);y<start+std::min(overlap-12,overlap*3/4);++y) {
        float total=0;int count=0;
        for(int row=y-8;row<=y+8;++row) {
            const float error=RowError(a[row+best.shift],b[row]);
            if(error>=0){total+=error;++count;}
        }
        if(count<4)continue;
        const float score=total/count+std::abs(y-start-overlap/2)*.002f;
        if(score<seamError){seamError=score;seam=y;}
    }
    if(seam<0 || seamError>6)return std::nullopt;
    return best.shift;
}
}
bool FramesStable(const Image& a,const Image& b) {
    if(a.width!=b.width || a.height!=b.height || !a.valid() || !b.valid())return false;
    std::uint64_t error=0;int changed=0,count=0;
    const int sx=std::max(1,a.width/160),sy=std::max(1,a.height/120);
    for(int y=0;y<a.height;y+=sy)for(int x=0;x<a.width;x+=sx) {
        const auto index=static_cast<std::size_t>(y)*a.width+x;
        const auto left=a.pixels[index],right=b.pixels[index];int difference=0;
        for(int shift:{0,8,16})difference+=std::abs(static_cast<int>((left>>shift)&255)-static_cast<int>((right>>shift)&255));
        error+=difference;if(difference>12)++changed;++count;
    }
    return count && static_cast<double>(error)/count<1.5 && changed<static_cast<double>(count)*.008;
}
void ScrollStitcher::Begin(Image frame) {
    previous_=frame;output_=std::move(frame);frames_=1;position_=0;
}
StitchResult ScrollStitcher::Append(Image frame) {
    if(!frame.valid() || frame.width!=previous_.width || frame.height!=previous_.height)return StitchResult::Uncertain;
    if(FramesStable(previous_,frame))return StitchResult::Unchanged;
    int seam=0;const auto matched=MatchShift(previous_,frame,seam);
    if(!matched)return StitchResult::Uncertain;
    const int shift=*matched;
    if(!shift){previous_=std::move(frame);return StitchResult::Unchanged;}
    const int next=position_+shift;
    const int top=std::min(0,next),bottom=std::max(output_.height,next+frame.height);
    const int height=bottom-top;
    const std::size_t pixels=static_cast<std::size_t>(height)*frame.width;
    if(height>MaxScrollHeight || pixels>MaxScrollBytes/4)return StitchResult::Limit;
    const int cut=next+seam;
    if(cut<0 || cut>output_.height)return StitchResult::Uncertain;
    if(height==output_.height) {
        position_=next;previous_=std::move(frame);++frames_;
        return StitchResult::Revisited; // Moving within covered content must not duplicate it.
    }
    if(output_.pixels.capacity()<pixels)output_.pixels.reserve(std::min(MaxScrollBytes/4,std::max(pixels,output_.pixels.capacity()+output_.pixels.capacity()/2)));
    const auto oldPixels=output_.pixels.size();output_.pixels.resize(pixels);
    if(top<0) {
        // Prepend above the old coverage, replacing the old fixed header at the overlap seam.
        std::move_backward(output_.pixels.begin()+static_cast<std::ptrdiff_t>(cut)*frame.width,
            output_.pixels.begin()+static_cast<std::ptrdiff_t>(oldPixels),output_.pixels.end());
        std::copy_n(frame.pixels.begin(),static_cast<std::size_t>(seam)*frame.width,output_.pixels.begin());
    }else {
        // Extend below coverage, replacing the old fixed footer at the overlap seam.
        std::copy(frame.pixels.begin()+static_cast<std::ptrdiff_t>(seam)*frame.width,frame.pixels.end(),
            output_.pixels.begin()+static_cast<std::ptrdiff_t>(cut)*frame.width);
    }
    output_.height=height;position_=next-top;previous_=std::move(frame);++frames_;
    return StitchResult::Appended;
}
}
