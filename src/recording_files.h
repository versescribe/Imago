#pragma once
#include <windows.h>
#include <memory>
#include <string>

namespace imago {
std::wstring RecordingFolder(std::wstring& error);
std::wstring NewRecordingPath(std::wstring& error);
bool CopyRecordingFile(HWND owner,const std::wstring& path,std::wstring& error);
struct RecordingExportStatus {bool done{},saved{};int percent{};std::wstring path,error;};
class RecordingExport {
public:
    RecordingExport();
    ~RecordingExport();
    bool Start(const std::wstring& source,const std::wstring& destination,std::wstring& error);
    void Cancel();
    void Join();
    RecordingExportStatus Status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
