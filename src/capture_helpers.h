#pragma once
#include "image.h"
namespace imago {
std::vector<RECT> SnapshotWindows(RECT desktop);
std::optional<RECT> WindowAt(const std::vector<RECT>& windows, POINT point);
}
