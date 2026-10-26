#include "updates.h"
#include "update_version.h"
#include "version.h"
#include <winhttp.h>
#include <shlobj.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>

namespace imago {
namespace {
constexpr size_t MaximumResponse=256*1024;
struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle(){if(value)WinHttpCloseHandle(value);}
};
struct Apartment {Apartment(){winrt::init_apartment();}~Apartment(){winrt::uninit_apartment();}};
UpdateResult Failure(const wchar_t* error){return {UpdateState::Failed,{},error};}
std::wstring UpdateStatePath() {
    PWSTR raw{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)))return {};
    struct FreePath {PWSTR value;~FreePath(){CoTaskMemFree(value);}} path{raw};
    return (std::filesystem::path(raw)/L"Imago"/L"updates.ini").wstring();
}
ULONGLONG NowSeconds() {
    FILETIME file{};GetSystemTimeAsFileTime(&file);
    ULARGE_INTEGER time{};time.LowPart=file.dwLowDateTime;time.HighPart=file.dwHighDateTime;
    return time.QuadPart/10000000;
}
UpdateResult FetchRelease(const std::atomic<bool>& cancelled) {
    InternetHandle session{WinHttpOpen(L"Imago/" IMAGO_VERSION_W,WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.value || !WinHttpSetTimeouts(session.value,3000,3000,3000,3000))return Failure(L"无法初始化系统网络服务。");
    InternetHandle connection{WinHttpConnect(session.value,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0)};
    if(!connection.value)return Failure(L"无法连接 GitHub，请检查网络或系统代理。");
    InternetHandle request{WinHttpOpenRequest(connection.value,L"GET",L"/repos/versescribe/Imago/releases/latest",
        nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    if(!request.value)return Failure(L"无法创建更新请求。");
    // Never follow arbitrary redirects or weaken Windows certificate validation.
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD authentication=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)) ||
       !WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&authentication,sizeof(authentication)))
        return Failure(L"无法配置安全的更新请求。");
    if(cancelled.load())return {UpdateState::Cancelled,{},{}};
    constexpr wchar_t headers[]=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n";
    if(!WinHttpSendRequest(request.value,headers,static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0) ||
       !WinHttpReceiveResponse(request.value,nullptr))return Failure(L"检查更新失败，请检查网络、系统代理或稍后重试。");
    DWORD status{},bytes=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX))return Failure(L"无法读取更新服务器响应。");
    if(status==404)return {UpdateState::NoRelease,{},L"尚无公开发布版本，或仓库暂时不可访问。"};
    if(status==403 || status==429)return Failure(L"GitHub 暂时限制了请求频率，请稍后再试。");
    if(status!=200)return Failure(L"GitHub 更新服务暂时不可用，请稍后再试。");
    std::string body;char buffer[8192];const ULONGLONG deadline=GetTickCount64()+15000;
    for(;;) {
        if(cancelled.load())return {UpdateState::Cancelled,{},{}};
        if(GetTickCount64()>deadline)return Failure(L"读取更新信息超时，请稍后重试。");
        DWORD read{};
        if(!WinHttpReadData(request.value,buffer,sizeof(buffer),&read))return Failure(L"更新信息接收中断，请重试。");
        if(!read)break;
        if(body.size()+read>MaximumResponse)return Failure(L"更新信息超出大小限制。");
        body.append(buffer,read);
    }
    Apartment apartment;
    return ParseRelease(body,IMAGO_VERSION_W);
}
}
UpdateResult ParseRelease(std::string_view json,std::wstring_view current) {
    try {
        if(json.empty() || json.size()>MaximumResponse)return Failure(L"更新信息为空或超出大小限制。");
        const auto release=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(json));
        if(release.GetNamedBoolean(L"draft") || release.GetNamedBoolean(L"prerelease"))return Failure(L"服务器未返回正式发布版本。");
        const std::wstring tag{release.GetNamedString(L"tag_name")};
        const auto remote=ParseVersion(tag),local=ParseVersion(current);
        if(!remote || !local || !tag.starts_with(L"v"))return Failure(L"更新版本号格式无效。");
        const auto version=tag.substr(1);
        if(*remote<=*local)return {UpdateState::Current,version,{}};
        const auto expected=L"Imago-"+version+L"-windows-x64.zip";
        for(const auto& value:release.GetNamedArray(L"assets")) {
            const auto asset=value.GetObject();
            if(asset.GetNamedString(L"name")==expected && asset.GetNamedString(L"state")==L"uploaded" && asset.GetNamedNumber(L"size")>0)
                return {UpdateState::Available,version,{}};
        }
        return Failure(L"新版尚无完整的 Windows x64 下载包，请稍后再试。");
    }catch(...){return Failure(L"无法解析 GitHub 更新信息。");}
}
bool AutomaticUpdateDue() {
    try {
        const auto path=UpdateStatePath();if(path.empty())return true;
        wchar_t text[32]{};GetPrivateProfileStringW(L"Updates",L"LastAttempt",L"0",text,32,path.c_str());
        wchar_t* end{};const auto previous=wcstoull(text,&end,10),now=NowSeconds();
        return *end!=L'\0' || previous>now || now-previous>=24*60*60;
    }catch(...){return true;}
}
void RememberUpdateAttempt() {
    try {
        const auto path=UpdateStatePath();if(path.empty())return;
        std::error_code error;std::filesystem::create_directories(std::filesystem::path(path).parent_path(),error);
        if(!error)WritePrivateProfileStringW(L"Updates",L"LastAttempt",std::to_wstring(NowSeconds()).c_str(),path.c_str());
    }catch(...){} // Failure to remember a check must not affect capture or startup.
}
struct UpdateChecker::Impl {
    std::mutex mutex;
    std::optional<UpdateResult> result;
    std::atomic<bool> busy{},cancelled{};
    std::jthread worker;
};
UpdateChecker::UpdateChecker():impl_(std::make_unique<Impl>()){}
UpdateChecker::~UpdateChecker(){Cancel();}
bool UpdateChecker::Start(HWND owner,UINT message) {
    if(impl_->busy.exchange(true))return false;
    if(impl_->worker.joinable())impl_->worker.join();
    impl_->cancelled=false;
    {std::lock_guard lock(impl_->mutex);impl_->result.reset();}
    try {
        impl_->worker=std::jthread([state=impl_.get(),owner,message] {
            UpdateResult result;
            try{result=FetchRelease(state->cancelled);}catch(...){result=Failure(L"更新检查未完成，系统资源或网络不可用。");}
            if(state->cancelled.load())result={UpdateState::Cancelled,{},{}};
            {std::lock_guard lock(state->mutex);state->result=std::move(result);}
            state->busy=false;
            if(!state->cancelled.load())PostMessageW(owner,message,0,0);
        });
        return true;
    }catch(...){impl_->busy=false;return false;}
}
bool UpdateChecker::Busy() const{return impl_->busy.load();}
std::optional<UpdateResult> UpdateChecker::TakeResult() {
    if(Busy())return {};
    std::lock_guard lock(impl_->mutex);auto result=std::move(impl_->result);impl_->result.reset();return result;
}
void UpdateChecker::Cancel(){impl_->cancelled=true;if(impl_->worker.joinable())impl_->worker.join();impl_->busy=false;}
}
