#include "image.h"
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace imago {
namespace {
struct DeleteBitmap { void operator()(HBITMAP value) const { if (value) DeleteObject(value); } };
using Bitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, DeleteBitmap>;
struct DeleteDc { void operator()(HDC value) const { if (value) DeleteDC(value); } };
using MemoryDc = std::unique_ptr<std::remove_pointer_t<HDC>, DeleteDc>;
struct ClipboardLock {
    bool open{};
    explicit ClipboardLock(HWND owner) {
        for (int i = 0; i < 5 && !open; ++i) {
            open = OpenClipboard(owner) != FALSE;
            if (!open && i != 4) Sleep(10);
        }
    }
    ~ClipboardLock() { if (open) CloseClipboard(); }
};
struct GlobalFreeDeleter { void operator()(void* value) const { if (value) GlobalFree(value); } };
using GlobalMemory = std::unique_ptr<void, GlobalFreeDeleter>;
GlobalMemory MakeGlobal(const std::vector<std::byte>& data) {
    GlobalMemory memory(GlobalAlloc(GMEM_MOVEABLE, data.size()));
    if (!memory) return {};
    void* destination = GlobalLock(memory.get());
    if (!destination) return {};
    std::memcpy(destination, data.data(), data.size());
    GlobalUnlock(memory.get());
    return memory;
}
std::uint8_t Channel(std::uint32_t pixel, std::uint32_t mask) {
    if (!mask) return 0;
    while ((mask & 1u) == 0u) { mask >>= 1u; pixel >>= 1u; }
    return static_cast<std::uint8_t>((static_cast<std::uint64_t>(pixel & mask) * 255u + mask / 2u) / mask);
}
bool ContiguousMask(std::uint32_t mask) {
    if (!mask) return false;
    while ((mask & 1u) == 0u) mask >>= 1u;
    return (mask & (mask + 1u)) == 0;
}
}

bool CopyText(HWND owner,const std::wstring& text,std::wstring& error) {
    const auto bytes=(text.size()+1)*sizeof(wchar_t);
    GlobalMemory memory(GlobalAlloc(GMEM_MOVEABLE,bytes));
    if(!memory){error=L"内存不足，无法复制文字。";return false;}
    void* data=GlobalLock(memory.get());
    if(!data){error=L"无法准备剪贴板数据。";return false;}
    std::memcpy(data,text.c_str(),bytes);GlobalUnlock(memory.get());
    ClipboardLock clipboard(owner);
    if(!clipboard.open || !EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,memory.get())) {
        error=L"剪贴板正忙，未能复制文字，请重试。";return false;
    }
    memory.release();return true;
}
std::optional<std::size_t> PixelCount(int width, int height) noexcept {
    if (width <= 0 || height <= 0) return {};
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > MaxImageBytes / sizeof(std::uint32_t)) return {};
    return static_cast<std::size_t>(count);
}
bool Image::valid() const noexcept {
    auto count = PixelCount(width, height);
    return count && pixels.size() == *count;
}
RECT NormalizeRect(POINT a, POINT b, int width, int height) noexcept {
    return {std::clamp(std::min(a.x, b.x), 0L, static_cast<LONG>(width)),
            std::clamp(std::min(a.y, b.y), 0L, static_cast<LONG>(height)),
            std::clamp(std::max(a.x, b.x), 0L, static_cast<LONG>(width)),
            std::clamp(std::max(a.y, b.y), 0L, static_cast<LONG>(height))};
}
RECT MoveRectWithin(RECT rect, int dx, int dy, int width, int height) noexcept {
    dx = std::clamp(dx, -static_cast<int>(rect.left), width - static_cast<int>(rect.right));
    dy = std::clamp(dy, -static_cast<int>(rect.top), height - static_cast<int>(rect.bottom));
    OffsetRect(&rect, dx, dy);
    return rect;
}
Image Crop(const Image& source, RECT rect) {
    if (!source.valid() || rect.left < 0 || rect.top < 0 || rect.right > source.width || rect.bottom > source.height)
        return {};
    auto count = PixelCount(rect.right - rect.left, rect.bottom - rect.top);
    if (!count) return {};
    Image result{rect.right - rect.left, rect.bottom - rect.top, std::vector<std::uint32_t>(*count)};
    for (int y = 0; y < result.height; ++y)
        std::copy_n(source.pixels.data() + static_cast<std::size_t>(rect.top + y) * source.width + rect.left,
                    result.width, result.pixels.data() + static_cast<std::size_t>(y) * result.width);
    return result;
}
Image CaptureDesktop(RECT& bounds, std::wstring& error) {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    bounds={x,y,x+width,y+height};
    return CaptureRegion(bounds,error);
}
Image CaptureRegion(RECT bounds,std::wstring& error) {
    const int x=bounds.left,y=bounds.top,width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    auto count = PixelCount(width, height);
    if (!count) { error = L"屏幕尺寸超出本版本支持的范围（原始图像上限 256 MiB）。"; return {}; }
    HDC screen = GetDC(nullptr);
    if (!screen) { error = L"无法读取当前桌面。"; return {}; }
    MemoryDc dc(CreateCompatibleDC(screen));
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits{};
    Bitmap bitmap(CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    bool captured = false;
    if (dc && bitmap && bits) {
        HGDIOBJ previous = SelectObject(dc.get(), bitmap.get());
        captured = BitBlt(dc.get(), 0, 0, width, height, screen, x, y, SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(dc.get(), previous);
    }
    ReleaseDC(nullptr, screen);
    if (!captured) { error = L"截图失败，请返回普通桌面后重试。"; return {}; }
    Image image{width, height, std::vector<std::uint32_t>(*count)};
    std::memcpy(image.pixels.data(), bits, image.pixels.size() * sizeof(std::uint32_t));
    for (auto& pixel : image.pixels) pixel |= 0xff000000u;
    return image;
}
Image Thumbnail(const Image& source,int maximum) {
    if(!source.valid() || maximum<1)return {};
    const double scale=std::min({1.0,static_cast<double>(maximum)/source.width,static_cast<double>(maximum)/source.height});
    const int width=std::max(1,static_cast<int>(source.width*scale)),height=std::max(1,static_cast<int>(source.height*scale));
    Image result{width,height,std::vector<std::uint32_t>(static_cast<std::size_t>(width)*height)};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const double sx=std::clamp((x+.5)/scale-.5,0.0,static_cast<double>(source.width-1));
        const double sy=std::clamp((y+.5)/scale-.5,0.0,static_cast<double>(source.height-1));
        const int x0=static_cast<int>(sx),y0=static_cast<int>(sy),x1=std::min(x0+1,source.width-1),y1=std::min(y0+1,source.height-1);
        const double fx=sx-x0,fy=sy-y0;
        const auto a=source.pixels[static_cast<std::size_t>(y0)*source.width+x0],b=source.pixels[static_cast<std::size_t>(y0)*source.width+x1];
        const auto c=source.pixels[static_cast<std::size_t>(y1)*source.width+x0],d=source.pixels[static_cast<std::size_t>(y1)*source.width+x1];
        std::uint32_t pixel=0xff000000u;
        for(int shift:{0,8,16})pixel|=static_cast<std::uint32_t>((((a>>shift)&255)*(1-fx)+((b>>shift)&255)*fx)*(1-fy)+
            (((c>>shift)&255)*(1-fx)+((d>>shift)&255)*fx)*fy+.5)<<shift;
        result.pixels[static_cast<std::size_t>(y)*width+x]=pixel;
    }
    return result;
}
std::vector<std::byte> EncodeDib(const Image& image, bool v5) {
    if (!image.valid()) return {};
    const std::size_t headerSize = v5 ? sizeof(BITMAPV5HEADER) : sizeof(BITMAPINFOHEADER);
    std::vector<std::byte> output(headerSize + image.pixels.size() * 4);
    BITMAPV5HEADER header{};
    header.bV5Size = static_cast<DWORD>(headerSize);
    header.bV5Width = image.width;
    header.bV5Height = -image.height;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = v5 ? BI_BITFIELDS : BI_RGB;
    header.bV5SizeImage = static_cast<DWORD>(image.pixels.size() * 4);
    if (v5) {
        header.bV5RedMask = 0x00ff0000;
        header.bV5GreenMask = 0x0000ff00;
        header.bV5BlueMask = 0x000000ff;
        header.bV5AlphaMask = 0xff000000;
        header.bV5CSType = LCS_sRGB;
        header.bV5Intent = LCS_GM_IMAGES;
    }
    std::memcpy(output.data(), &header, headerSize);
    std::memcpy(output.data() + headerSize, image.pixels.data(), image.pixels.size() * 4);
    return output;
}
Image DecodeDib(const void* data, std::size_t size) {
    if (!data || size < sizeof(BITMAPINFOHEADER)) return {};
    BITMAPINFOHEADER header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.biSize < sizeof(header) || header.biSize > size || header.biPlanes != 1 ||
        (header.biBitCount != 24 && header.biBitCount != 32) ||
        (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS) ||
        header.biHeight == std::numeric_limits<LONG>::min()) return {};
    const int height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
    auto count = PixelCount(header.biWidth, height);
    if (!count) return {};
    std::size_t offset = header.biSize;
    const auto* bytes = static_cast<const std::byte*>(data);
    std::uint32_t masks[4]{0x00ff0000, 0x0000ff00, 0x000000ff, 0};
    if (header.biCompression == BI_BITFIELDS) {
        if (header.biBitCount != 32) return {};
        if (header.biSize == sizeof(BITMAPINFOHEADER)) {
            if (size - offset < 12) return {};
            std::memcpy(masks, bytes + offset, 12);
            offset += 12;
        } else if (header.biSize >= 52) {
            std::memcpy(masks, bytes + sizeof(BITMAPINFOHEADER), header.biSize >= 56 ? 16 : 12);
        } else return {};
        if (!ContiguousMask(masks[0]) || !ContiguousMask(masks[1]) || !ContiguousMask(masks[2]) ||
            (masks[0] & masks[1]) || (masks[0] & masks[2]) || (masks[1] & masks[2]) ||
            (masks[3] && (!ContiguousMask(masks[3]) || (masks[3] & (masks[0] | masks[1] | masks[2]))))) return {};
    }
    const auto tableBytes = static_cast<std::uint64_t>(header.biClrUsed) * sizeof(RGBQUAD);
    if (tableBytes > size - offset) return {};
    offset += static_cast<std::size_t>(tableBytes);
    // Embedded color profiles are not interpreted; reject rather than treating profile bytes as pixels.
    if (header.biSize >= sizeof(BITMAPV5HEADER)) {
        BITMAPV5HEADER v5{};
        std::memcpy(&v5, data, sizeof(v5));
        if (v5.bV5ProfileData || v5.bV5ProfileSize) return {};
    }
    const std::size_t stride = ((static_cast<std::size_t>(header.biWidth) * header.biBitCount + 31) / 32) * 4;
    if (stride > (size - offset) / static_cast<std::size_t>(height)) return {};
    Image output{header.biWidth, height, std::vector<std::uint32_t>(*count)};
    for (int y = 0; y < height; ++y) {
        const int sourceY = header.biHeight < 0 ? y : height - 1 - y;
        const auto* row = bytes + offset + static_cast<std::size_t>(sourceY) * stride;
        for (int x = 0; x < output.width; ++x) {
            std::uint32_t pixel{};
            std::memcpy(&pixel, row + static_cast<std::size_t>(x) * (header.biBitCount / 8), header.biBitCount / 8);
            auto r = Channel(pixel, masks[0]), g = Channel(pixel, masks[1]), b = Channel(pixel, masks[2]);
            if (masks[3]) {
                const unsigned a = Channel(pixel, masks[3]);
                r = static_cast<std::uint8_t>((r * a + 255u * (255u - a) + 127u) / 255u);
                g = static_cast<std::uint8_t>((g * a + 255u * (255u - a) + 127u) / 255u);
                b = static_cast<std::uint8_t>((b * a + 255u * (255u - a) + 127u) / 255u);
            }
            output.pixels[static_cast<std::size_t>(y) * output.width + x] =
                0xff000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
        }
    }
    return output;
}
bool CopyImage(HWND owner, const Image& image, std::wstring& error) {
    if (!image.valid()) { error = L"没有可复制的图像。"; return false; }
    auto v5 = MakeGlobal(EncodeDib(image, true)), dib = MakeGlobal(EncodeDib(image, false));
    if (!v5 || !dib) { error = L"内存不足，无法复制图像。"; return false; }
    ClipboardLock clipboard(owner);
    if (!clipboard.open) { error = L"剪贴板正被其他程序使用，请稍后重试。"; return false; }
    if (!EmptyClipboard()) { error = L"无法更新剪贴板。"; return false; }
    bool copied = false;
    if (SetClipboardData(CF_DIB, dib.get())) { dib.release(); copied = true; }
    if (SetClipboardData(CF_DIBV5, v5.get())) { v5.release(); copied = true; }
    if (!copied) error = L"Windows 未能接收图像，请重试。";
    return copied;
}
Image ReadClipboardImage(HWND owner, std::wstring& error) {
    ClipboardLock clipboard(owner);
    if (!clipboard.open) { error = L"剪贴板正被其他程序使用，请稍后重试。"; return {}; }
    for (UINT format : {CF_DIBV5, CF_DIB}) {
        HANDLE handle = GetClipboardData(format);
        if (!handle) continue;
        void* data = GlobalLock(handle);
        if (!data) continue;
        Image result;
        try { result = DecodeDib(data, GlobalSize(handle)); }
        catch (...) { GlobalUnlock(handle); throw; }
        GlobalUnlock(handle);
        if (result.valid()) return result;
    }
    HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    BITMAP details{};
    if (bitmap && GetObjectW(bitmap, sizeof(details), &details)) {
        auto count = PixelCount(details.bmWidth, details.bmHeight);
        if (count) {
            Image result{details.bmWidth, details.bmHeight, std::vector<std::uint32_t>(*count)};
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER), result.width, -result.height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
            MemoryDc dc(CreateCompatibleDC(nullptr));
            if (dc && GetDIBits(dc.get(), bitmap, 0, result.height, result.pixels.data(), &info, DIB_RGB_COLORS) == result.height) {
                for (auto& pixel : result.pixels) pixel |= 0xff000000u;
                return result;
            }
        }
    }
    error = L"剪贴板中没有可用的位图。请先复制图片，或截图后按 P 贴图。";
    return {};
}
}
