#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace gmdr {

// Owned OS file handle with positional reads and writes (no shared cursor, safe across threads).
// Files are opened with full sharing so one process can read a file that another is still writing.
class File {
public:
    enum class Mode { Read, CreateTruncate, ReadWrite };

    File() = default;
    ~File();
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    static Result<File> open(const std::filesystem::path& path, Mode mode);
    // Adopts a handle inherited from a parent process (Windows HANDLE value or POSIX fd).
    static File adopt(std::intptr_t nativeHandle);

    Result<std::size_t> readAt(std::uint64_t offset, std::span<std::uint8_t> out) const;
    // Reads exactly out.size() bytes or fails.
    Result<void> readExactAt(std::uint64_t offset, std::span<std::uint8_t> out) const;
    Result<void> writeAt(std::uint64_t offset, std::span<const std::uint8_t> data);
    Result<std::uint64_t> size() const;
    Result<void> flush();

    bool valid() const;
    std::intptr_t nativeHandle() const { return handle_; }
    // Marks the handle inheritable by child processes (needed before spawning with it).
    Result<void> setInheritable(bool inheritable);
    // Gives up ownership without closing (for handles owned elsewhere).
    std::intptr_t release();
    void close();

private:
    explicit File(std::intptr_t h) : handle_(h) {}
    std::intptr_t handle_ = -1;
};

// Read-only memory map of a whole file.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    static Result<MappedFile> map(const File& file);

    std::span<const std::uint8_t> data() const { return {static_cast<const std::uint8_t*>(view_), size_}; }

private:
    void reset();
    const void* view_ = nullptr;
    std::size_t size_ = 0;
    std::intptr_t mapping_ = 0;
};

Result<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& path, std::size_t maxBytes);

} // namespace gmdr
