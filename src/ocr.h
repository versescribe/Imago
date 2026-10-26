#pragma once
#include "image.h"
#include <memory>

namespace imago {
enum class OcrStatus { Success, NoText, NoLanguage, Unavailable, Failed, Cancelled };
struct OcrResult {
    OcrStatus status{OcrStatus::Failed};
    std::wstring text,error,language;
    bool resized{};
};
// A single cancellable worker. Results stay in owned storage; window messages carry only an ID.
class OcrTask {
public:
    OcrTask();
    ~OcrTask();
    OcrTask(const OcrTask&)=delete;
    OcrTask& operator=(const OcrTask&)=delete;
    bool Start(Image image,HWND notifyWindow,UINT message,std::wstring& error);
    void Cancel();
    std::optional<OcrResult> TakeResult(WPARAM request);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
