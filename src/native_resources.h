#pragma once
#include <windows.h>
#include <utility>

namespace imago {
// Only owned GDI objects belong here. Stock objects and HWNDs remain borrowed.
template<class T>
class UniqueGdiObject {
public:
    UniqueGdiObject() = default;
    explicit UniqueGdiObject(T object) noexcept : object_(object) {}
    ~UniqueGdiObject() { reset(); }
    UniqueGdiObject(const UniqueGdiObject&) = delete;
    UniqueGdiObject& operator=(const UniqueGdiObject&) = delete;
    UniqueGdiObject(UniqueGdiObject&& other) noexcept : object_(other.release()) {}
    UniqueGdiObject& operator=(UniqueGdiObject&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    T get() const noexcept { return object_; }
    explicit operator bool() const noexcept { return object_ != nullptr; }
    T release() noexcept { return std::exchange(object_, nullptr); }
    void reset(T object = nullptr) noexcept {
        if (object_ != object) {
            if (object_) DeleteObject(object_);
            object_ = object;
        }
    }
private:
    T object_{};
};
using UniqueFont = UniqueGdiObject<HFONT>;
using UniqueBrush = UniqueGdiObject<HBRUSH>;
using UniquePen = UniqueGdiObject<HPEN>;

class ScopedSelection {
public:
    ScopedSelection(HDC dc, HGDIOBJ object) : dc_(dc), previous_(SelectObject(dc, object)) {}
    ~ScopedSelection() {
        if (previous_ && previous_ != HGDI_ERROR) SelectObject(dc_, previous_);
    }
    ScopedSelection(const ScopedSelection&) = delete;
    ScopedSelection& operator=(const ScopedSelection&) = delete;
private:
    HDC dc_;
    HGDIOBJ previous_;
};

class ScopedDcState {
public:
    explicit ScopedDcState(HDC dc) : dc_(dc), saved_(SaveDC(dc)) {}
    ~ScopedDcState() { if (saved_) RestoreDC(dc_, saved_); }
    ScopedDcState(const ScopedDcState&) = delete;
    ScopedDcState& operator=(const ScopedDcState&) = delete;
private:
    HDC dc_;
    int saved_;
};
}
