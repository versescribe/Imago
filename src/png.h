#pragma once
#include "image.h"
namespace imago {
enum class SaveResult { Saved, Cancelled, Failed };
SaveResult SavePngDialog(HWND owner,const Image& image,std::wstring& error);
}
