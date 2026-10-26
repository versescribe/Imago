#pragma once
#include "image.h"
#include <d2d1.h>
#include <dwrite.h>
#include <optional>
#include <map>

namespace imago {
enum class AnnotationKind { Rectangle, Ellipse, Arrow, Text, Redact, Mosaic };
enum class MosaicStyle { Pixelate, Blur };
struct Annotation {
    AnnotationKind kind{AnnotationKind::Rectangle};
    float x1{}, y1{}, x2{}, y2{};
    COLORREF color{RGB(232, 17, 35)};
    float stroke{3.f};
    float fontSize{24.f};
    int blockSize{12};
    MosaicStyle mosaicStyle{MosaicStyle::Pixelate};
    std::wstring text;
    bool operator==(const Annotation&) const = default;
};

// Transactions record object data only. A drag or text edit is a single undo step.
class AnnotationDocument {
public:
    std::vector<Annotation> items;
    std::optional<std::size_t> active;
    void Begin();
    void Commit();
    void Cancel();
    void Undo();
    void Redo();
    void DeleteActive();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
private:
    using Snapshot = std::vector<Annotation>;
    std::optional<Snapshot> before_;
    std::vector<Snapshot> undo_, redo_;
    static void Trim(std::vector<Snapshot>& stack);
};

D2D1_RECT_F AnnotationBounds(const Annotation& item);
std::optional<std::size_t> HitAnnotation(const std::vector<Annotation>& items, D2D1_POINT_2F point, float tolerance);
int HitAnnotationHandle(const Annotation& item, D2D1_POINT_2F point, float tolerance);
// Cache downsampled source pixels, not whole edited screenshots, outside undo history.
class MosaicCache {
public:
    const Image& Get(const Image& source, int blockSize, MosaicStyle style);
private:
    const std::uint32_t* source_{};
    std::map<std::pair<MosaicStyle,int>,Image> images_;
};
HRESULT DrawAnnotations(ID2D1RenderTarget* target, IDWriteFactory* textFactory, const Image& source, MosaicCache& mosaics,
                        const std::vector<Annotation>& items, std::optional<std::size_t> exclude = {});
Image Composite(const Image& desktop, RECT crop, const std::vector<Annotation>& items,
                ID2D1Factory* factory, IDWriteFactory* textFactory, MosaicCache& mosaics, std::wstring& error);
}
