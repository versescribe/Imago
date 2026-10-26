#pragma once
#include <windows.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace imago {
inline constexpr wchar_t ReleasesPage[]=L"https://github.com/versescribe/Imago/releases/latest";
enum class UpdateState { Current, Available, NoRelease, Failed, Cancelled };
struct UpdateResult {
    UpdateState state{UpdateState::Failed};
    std::wstring version,error;
};
// Kept separate from transport so untrusted release metadata can be tested without networking.
UpdateResult ParseRelease(std::string_view json,std::wstring_view current);
bool AutomaticUpdateDue();
void RememberUpdateAttempt();
class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();
    bool Start(HWND owner,UINT message);
    bool Busy() const;
    std::optional<UpdateResult> TakeResult();
    void Cancel();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
