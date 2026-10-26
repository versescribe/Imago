#pragma once
#include <cstddef>
#include <span>
#include <string>

namespace imago {
enum class FileWriteResult { Saved, CreateFailed, WriteFailed, ReplaceFailed };
// Writes a unique temporary file beside the destination, flushes and closes it,
// then replaces the destination. On failure, removes only our own temporary file.
FileWriteResult WriteFileAtomically(const std::wstring& path, std::span<const std::byte> bytes);
}
