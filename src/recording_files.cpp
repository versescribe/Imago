#include "recording_files.h"
#include <shlobj.h>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace imago {
namespace {
struct ShellString {PWSTR value{};~ShellString(){CoTaskMemFree(value);}};
struct File {HANDLE value{INVALID_HANDLE_VALUE};~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Memory {HGLOBAL value{};~Memory(){if(value)GlobalFree(value);}void Release(){value=nullptr;}};
struct Clipboard {bool open{};~Clipboard(){if(open)CloseClipboard();}};
struct TemporaryFile {std::wstring path;bool owned{};~TemporaryFile(){if(owned)DeleteFileW(path.c_str());}};
std::wstring UniqueId() {
    GUID id{};wchar_t text[40]{};
    if(FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id,text,40))return {};
    return std::wstring(text+1,36);
}
}
std::wstring RecordingFolder(std::wstring& error) {
    ShellString folder;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder.value))){error=L"无法定位录像暂存目录。";return {};}
    const auto path=std::filesystem::path(folder.value)/L"Imago"/L"Recordings";
    std::error_code code;std::filesystem::create_directories(path,code);
    if(code){error=L"无法创建录像暂存目录，请检查磁盘空间和权限。";return {};}
    return path.wstring();
}
std::wstring NewRecordingPath(std::wstring& error) {
    const auto folder=RecordingFolder(error);if(folder.empty())return {};
    const auto id=UniqueId();if(id.empty()){error=L"无法创建录像文件名。";return {};}
    SYSTEMTIME time{};GetLocalTime(&time);wchar_t name[64]{};
    swprintf_s(name,L"Imago-%04u%02u%02u-%02u%02u%02u-",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    return (std::filesystem::path(folder)/(name+id+L".mp4")).wstring();
}
bool CopyRecordingFile(HWND owner,const std::wstring& path,std::wstring& error) {
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)){error=L"录像文件已被移动或删除，无法复制。";return false;}
    const SIZE_T bytes=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);
    Memory files{GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes)};
    if(!files.value){error=L"内存不足，无法复制录像。";return false;}
    auto* drop=static_cast<DROPFILES*>(GlobalLock(files.value));
    if(!drop){error=L"无法准备录像剪贴板数据。";return false;}
    drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;
    std::memcpy(reinterpret_cast<BYTE*>(drop)+sizeof(DROPFILES),path.c_str(),(path.size()+1)*sizeof(wchar_t));GlobalUnlock(files.value);
    const UINT preferred=RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    Memory effect{GlobalAlloc(GMEM_MOVEABLE,sizeof(DWORD))};
    if(effect.value){auto* value=static_cast<DWORD*>(GlobalLock(effect.value));if(value){*value=DROPEFFECT_COPY;GlobalUnlock(effect.value);}else {GlobalFree(effect.value);effect.value=nullptr;}}
    Clipboard clipboard{OpenClipboard(owner)!=FALSE};
    if(!clipboard.open || !EmptyClipboard() || !SetClipboardData(CF_HDROP,files.value)) {
        error=L"剪贴板正忙，未能复制录像，请重试。";return false;
    }
    files.Release();
    if(preferred && effect.value && SetClipboardData(preferred,effect.value))effect.Release();
    return true;
}
struct RecordingExport::Impl {
    mutable std::mutex mutex;
    RecordingExportStatus status;
    std::atomic<bool> cancel{};
    std::jthread worker;
    static DWORD CALLBACK Progress(LARGE_INTEGER total,LARGE_INTEGER transferred,LARGE_INTEGER,LARGE_INTEGER,
        DWORD,DWORD,HANDLE,HANDLE,LPVOID context) noexcept {
        auto& self=*static_cast<Impl*>(context);
        if(self.cancel.load())return PROGRESS_CANCEL;
        const int percent=total.QuadPart?static_cast<int>(std::clamp(100.0*transferred.QuadPart/total.QuadPart,0.0,99.0)):0;
        try{std::lock_guard lock(self.mutex);self.status.percent=percent;}catch(...){return PROGRESS_CANCEL;}
        return PROGRESS_CONTINUE;
    }
    void Run(const std::wstring& source,const std::wstring& destination) noexcept {
        bool saved=false;std::wstring error;
        try {
            const auto id=UniqueId();if(id.empty())throw std::runtime_error("unique filename");
            TemporaryFile temporary{(std::filesystem::path(destination).parent_path()/(L".imago-save-"+id+L".tmp")).wstring()};
            {File file{CreateFileW(temporary.path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
                temporary.owned=file.value!=INVALID_HANDLE_VALUE;}
            if(!temporary.owned)error=L"无法写入目标目录，请选择其他位置。";
            else if(cancel.load())error=L"已取消保存，录像仍保留在暂存目录。";
            else if(!CopyFileExW(source.c_str(),temporary.path.c_str(),Progress,this,nullptr,0))
                error=cancel.load()?L"已取消保存，录像仍保留在暂存目录。":L"保存录像失败，请检查磁盘空间、原录像和目录权限。";
            else {
                bool flushed=false;
                {File file{CreateFileW(temporary.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
                    flushed=file.value!=INVALID_HANDLE_VALUE && FlushFileBuffers(file.value);}
                if(cancel.load())error=L"已取消保存，录像仍保留在暂存目录。";
                else if(!flushed)error=L"无法完成录像写入，请检查磁盘空间。";
                else if(!MoveFileExW(temporary.path.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    error=L"无法替换目标文件，请检查文件是否正在被占用。";
                else {temporary.owned=false;saved=true;}
            }
        }catch(...){error=L"系统资源不足，未能保存录像。原录像仍保留在暂存目录。";}
        std::lock_guard lock(mutex);status.saved=saved;status.percent=saved?100:status.percent;status.error=std::move(error);status.done=true;
    }
};
RecordingExport::RecordingExport():impl_(std::make_unique<Impl>()){}
RecordingExport::~RecordingExport(){Cancel();Join();}
bool RecordingExport::Start(const std::wstring& source,const std::wstring& destination,std::wstring& error) {
    Cancel();Join();impl_->cancel=false;
    {std::lock_guard lock(impl_->mutex);impl_->status={false,false,0,destination,{}};}
    try{impl_->worker=std::jthread([state=impl_.get(),source,destination]{state->Run(source,destination);});return true;}
    catch(...){error=L"无法启动录像保存任务。";return false;}
}
void RecordingExport::Cancel(){impl_->cancel=true;}
void RecordingExport::Join(){if(impl_->worker.joinable())impl_->worker.join();}
RecordingExportStatus RecordingExport::Status() const {std::lock_guard lock(impl_->mutex);return impl_->status;}
}
