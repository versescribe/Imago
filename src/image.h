#pragma once
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace imago {
// Every image is opaque, top-down BGRA. Coordinates are physical screen pixels.
struct Image {
    int width{};
    int height{};
    std::vector<std::uint32_t> pixels;
    bool valid() const noexcept;
};
constexpr std::size_t MaxImageBytes = 256u * 1024u * 1024u;
std::optional<std::size_t> PixelCount(int width, int height) noexcept;
RECT NormalizeRect(POINT a, POINT b, int width, int height) noexcept;
RECT MoveRectWithin(RECT rect, int dx, int dy, int width, int height) noexcept;
Image Crop(const Image& source, RECT rect);
Image CaptureDesktop(RECT& bounds, std::wstring& error);
Image CaptureRegion(RECT bounds, std::wstring& error);
Image Thumbnail(const Image& source, int maximum);
std::vector<std::byte> EncodeDib(const Image& image, bool v5);
Image DecodeDib(const void* data, std::size_t size);
bool CopyImage(HWND owner, const Image& image, std::wstring& error);
bool CopyText(HWND owner, const std::wstring& text, std::wstring& error);
Image ReadClipboardImage(HWND owner, std::wstring& error);
}
