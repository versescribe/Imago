#include "image.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

void Require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
bool Equal(RECT a, RECT b) { return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }

int main(int argc, char** argv) {
    try {
        using namespace imago;
        if (argc > 1 && std::string(argv[1]) == "--clipboard-info") {
            std::wstring error;
            auto image = ReadClipboardImage(nullptr, error);
            Require(image.valid(), "Clipboard contains a valid image");
            std::cout << image.width << 'x' << image.height;
            for (auto color : {0xffff0000u, 0xff00ff00u, 0xff0000ffu})
                std::cout << ' ' << std::hex << color << ':' << std::dec << std::count(image.pixels.begin(), image.pixels.end(), color);
            std::uint64_t hash = 14695981039346656037ull;
            for (auto pixel : image.pixels) { hash ^= pixel; hash *= 1099511628211ull; }
            std::cout << " hash=" << std::hex << hash << '\n';
            return 0;
        }
        Require(!PixelCount(-1,10) && !PixelCount(0,10),"Reject nonpositive dimensions");
        Require(!PixelCount(INT_MAX,INT_MAX),"Reject oversized allocations without integer overflow");
        Require(PixelCount(3840,2160).value()==8294400,"4K frame size");
        Require(Equal(NormalizeRect({90,80},{10,20},100,100),{10,20,90,80}),"Reverse-direction selection");
        Require(Equal(NormalizeRect({-50,-20},{120,140},100,80),{0,0,100,80}),"Clip selection to image bounds");
        Require(Equal(MoveRectWithin({10,10,40,30},-100,100,100,80),{0,60,30,80}),"Move without changing size at boundaries");
        Image image{3,2,{0xffff0000,0xff00ff00,0xff0000ff,0xffffffff,0xff000000,0xff123456}};
        auto crop=Crop(image,{1,0,3,2});
        Require(crop.width==2 && crop.height==2 && crop.pixels==std::vector<std::uint32_t>{0xff00ff00,0xff0000ff,0xff000000,0xff123456},"Crop rows in top-down order");
        Require(!Crop(image,{-1,0,2,2}).valid() && !Crop(image,{2,0,1,2}).valid(),"Reject out-of-bounds and empty crops");
        for(bool v5:{false,true}) {
            auto encoded=EncodeDib(image,v5);
            auto decoded=DecodeDib(encoded.data(),encoded.size());
            Require(decoded.valid() && decoded.pixels==image.pixels,"Clipboard DIB round trip preserves pixels");
            Require(!DecodeDib(encoded.data(),encoded.size()-1).valid(),"Reject truncated pixel data");
            for(std::size_t length=0;length<sizeof(BITMAPINFOHEADER);++length)
                Require(!DecodeDib(encoded.data(),length).valid(),"Reject short headers");
            BITMAPINFOHEADER header{};std::memcpy(&header,encoded.data(),sizeof(header));
            header.biHeight=std::numeric_limits<LONG>::min();std::memcpy(encoded.data(),&header,sizeof(header));
            Require(!DecodeDib(encoded.data(),encoded.size()).valid(),"Reject height negation overflow");
        }
        // 24-bit bottom-up DIB with padded rows, as supplied by other Windows apps.
        BITMAPINFOHEADER header{sizeof(header),1,2,1,24,BI_RGB,8,0,0,0,0};
        std::vector<std::byte> dib(sizeof(header)+8);
        std::memcpy(dib.data(),&header,sizeof(header));
        unsigned char rows[]{255,0,0,0,0,0,255,0};std::memcpy(dib.data()+sizeof(header),rows,8);
        auto decoded=DecodeDib(dib.data(),dib.size());
        Require(decoded.pixels==std::vector<std::uint32_t>{0xffff0000,0xff0000ff},"24-bit bottom-up row order and padding");
        auto masked=EncodeDib(image,true);
        BITMAPV5HEADER v5{};std::memcpy(&v5,masked.data(),sizeof(v5));
        v5.bV5GreenMask=v5.bV5RedMask;std::memcpy(masked.data(),&v5,sizeof(v5));
        Require(!DecodeDib(masked.data(),masked.size()).valid(),"Reject overlapping color masks");
        auto alpha=EncodeDib(Image{1,1,{0x80ff0000}},true);
        Require(DecodeDib(alpha.data(),alpha.size()).pixels[0]==0xffff7f7f,"Flatten alpha onto white for opaque pins");
        // Malformed clipboard headers are untrusted: exercise parser without touching the live clipboard.
        std::mt19937 random(42);
        for(int i=0;i<5000;++i) {
            auto bytes=EncodeDib(image,i%2==0);
            for(int j=0;j<6;++j)bytes[random()%bytes.size()]=static_cast<std::byte>(random()&255);
            auto result=DecodeDib(bytes.data(),bytes.size());
            Require(result.pixels.empty() || result.valid(),"Malformed input cannot return inconsistent image");
        }
        std::cout<<"PASS: geometry, crop, DIB/DIBV5, padding, alpha, bounds, and 5000 malformed clipboard cases\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;
    }
}
