#include "file_io.h"
#include <windows.h>
#include <objbase.h>
#include <filesystem>

namespace imago {
namespace {
class FileHandle {
public:
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const { return handle_; }
private:
    HANDLE handle_;
};
struct TemporaryFile {
    std::wstring path;
    bool created{};
    ~TemporaryFile() { if (created) DeleteFileW(path.c_str()); }
};
}

FileWriteResult WriteFileAtomically(const std::wstring& path, std::span<const std::byte> bytes) {
    if (path.empty() || bytes.size() > MAXDWORD) return FileWriteResult::CreateFailed;
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return FileWriteResult::CreateFailed;
    wchar_t suffix[40]{};
    if (!StringFromGUID2(guid, suffix, 40)) return FileWriteResult::CreateFailed;
    const auto folder = std::filesystem::path(path).parent_path();
    TemporaryFile temporary{(folder / (std::wstring(L".imago-") + suffix + L".tmp")).wstring()};
    {
        FileHandle file(CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) return FileWriteResult::CreateFailed;
        temporary.created = true;
        DWORD written{};
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.get())) return FileWriteResult::WriteFailed;
    }
    if (!MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return FileWriteResult::ReplaceFailed;
    temporary.created = false;
    return FileWriteResult::Saved;
}
}
