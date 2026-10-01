#include "core/file.h"

#include "core/text.h"

#include <algorithm>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gmdr {

namespace {

#ifdef _WIN32
constexpr std::intptr_t kInvalid = -1;

HANDLE toHandle(std::intptr_t h) {
    return reinterpret_cast<HANDLE>(h);
}

Error lastError(const char* code, const std::string& what) {
    return makeError(code, what, "win32 error " + std::to_string(GetLastError()));
}
#else
constexpr std::intptr_t kInvalid = -1;

Error lastError(const char* code, const std::string& what) {
    return makeError(code, what, std::strerror(errno));
}
#endif

} // namespace

File::~File() {
    close();
}

File::File(File&& other) noexcept : handle_(std::exchange(other.handle_, kInvalid)) {}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, kInvalid);
    }
    return *this;
}

bool File::valid() const {
    return handle_ != kInvalid;
}

std::intptr_t File::release() {
    return std::exchange(handle_, kInvalid);
}

File File::adopt(std::intptr_t nativeHandle) {
    return File(nativeHandle);
}

#ifdef _WIN32

Result<File> File::open(const std::filesystem::path& path, Mode mode) {
    DWORD access = GENERIC_READ;
    DWORD disposition = OPEN_EXISTING;
    if (mode == Mode::CreateTruncate) {
        access = GENERIC_READ | GENERIC_WRITE;
        disposition = CREATE_ALWAYS;
    } else if (mode == Mode::ReadWrite) {
        access = GENERIC_READ | GENERIC_WRITE;
        disposition = OPEN_EXISTING;
    }
    HANDLE h = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        const bool missing = err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
        return makeError(missing ? "core.file_not_found" : "core.file_open", "cannot open file",
                         pathToUtf8(path) + " (win32 error " + std::to_string(err) + ")");
    }
    return File(reinterpret_cast<std::intptr_t>(h));
}

Result<std::size_t> File::readAt(std::uint64_t offset, std::span<std::uint8_t> out) const {
    std::size_t total = 0;
    while (total < out.size()) {
        OVERLAPPED ov{};
        const std::uint64_t pos = offset + total;
        ov.Offset = static_cast<DWORD>(pos & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(out.size() - total, 1u << 30));
        DWORD got = 0;
        if (!ReadFile(toHandle(handle_), out.data() + total, want, &got, &ov)) {
            if (GetLastError() == ERROR_HANDLE_EOF)
                break;
            return lastError("core.file_read", "read failed");
        }
        if (got == 0)
            break;
        total += got;
    }
    return total;
}

Result<void> File::writeAt(std::uint64_t offset, std::span<const std::uint8_t> data) {
    std::size_t total = 0;
    while (total < data.size()) {
        OVERLAPPED ov{};
        const std::uint64_t pos = offset + total;
        ov.Offset = static_cast<DWORD>(pos & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(data.size() - total, 1u << 30));
        DWORD put = 0;
        if (!WriteFile(toHandle(handle_), data.data() + total, want, &put, &ov) || put == 0)
            return lastError("core.file_write", "write failed");
        total += put;
    }
    return {};
}

Result<std::uint64_t> File::size() const {
    LARGE_INTEGER s{};
    if (!GetFileSizeEx(toHandle(handle_), &s))
        return lastError("core.file_size", "cannot get file size");
    return static_cast<std::uint64_t>(s.QuadPart);
}

Result<void> File::flush() {
    if (!FlushFileBuffers(toHandle(handle_)))
        return lastError("core.file_flush", "flush failed");
    return {};
}

Result<void> File::setInheritable(bool inheritable) {
    if (!SetHandleInformation(toHandle(handle_), HANDLE_FLAG_INHERIT, inheritable ? HANDLE_FLAG_INHERIT : 0))
        return lastError("core.file_inherit", "cannot change handle inheritance");
    return {};
}

void File::close() {
    if (handle_ != kInvalid) {
        CloseHandle(toHandle(handle_));
        handle_ = kInvalid;
    }
}

MappedFile::~MappedFile() {
    reset();
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : view_(std::exchange(other.view_, nullptr)), size_(std::exchange(other.size_, 0)),
      mapping_(std::exchange(other.mapping_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        reset();
        view_ = std::exchange(other.view_, nullptr);
        size_ = std::exchange(other.size_, 0);
        mapping_ = std::exchange(other.mapping_, 0);
    }
    return *this;
}

void MappedFile::reset() {
    if (view_)
        UnmapViewOfFile(view_);
    if (mapping_)
        CloseHandle(reinterpret_cast<HANDLE>(mapping_));
    view_ = nullptr;
    size_ = 0;
    mapping_ = 0;
}

Result<MappedFile> MappedFile::map(const File& file) {
    auto size = file.size();
    if (!size)
        return size.error();
    MappedFile m;
    if (*size == 0)
        return m;
    HANDLE mapping = CreateFileMappingW(toHandle(file.nativeHandle()), nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping)
        return lastError("core.mmap", "CreateFileMapping failed");
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        return lastError("core.mmap", "MapViewOfFile failed");
    }
    m.view_ = view;
    m.size_ = static_cast<std::size_t>(*size);
    m.mapping_ = reinterpret_cast<std::intptr_t>(mapping);
    return m;
}

#else // POSIX

Result<File> File::open(const std::filesystem::path& path, Mode mode) {
    int flags = O_RDONLY | O_CLOEXEC;
    if (mode == Mode::CreateTruncate)
        flags = O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC;
    else if (mode == Mode::ReadWrite)
        flags = O_RDWR | O_CLOEXEC;
    const int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0)
        return makeError(errno == ENOENT ? "core.file_not_found" : "core.file_open", "cannot open file",
                         pathToUtf8(path) + " (" + std::strerror(errno) + ")");
    return File(fd);
}

Result<std::size_t> File::readAt(std::uint64_t offset, std::span<std::uint8_t> out) const {
    std::size_t total = 0;
    while (total < out.size()) {
        const ssize_t n = ::pread(static_cast<int>(handle_), out.data() + total, out.size() - total,
                                  static_cast<off_t>(offset + total));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return lastError("core.file_read", "read failed");
        }
        if (n == 0)
            break;
        total += static_cast<std::size_t>(n);
    }
    return total;
}

Result<void> File::writeAt(std::uint64_t offset, std::span<const std::uint8_t> data) {
    std::size_t total = 0;
    while (total < data.size()) {
        const ssize_t n = ::pwrite(static_cast<int>(handle_), data.data() + total, data.size() - total,
                                   static_cast<off_t>(offset + total));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return lastError("core.file_write", "write failed");
        }
        total += static_cast<std::size_t>(n);
    }
    return {};
}

Result<std::uint64_t> File::size() const {
    struct stat st {};
    if (::fstat(static_cast<int>(handle_), &st) != 0)
        return lastError("core.file_size", "cannot get file size");
    return static_cast<std::uint64_t>(st.st_size);
}

Result<void> File::flush() {
    if (::fsync(static_cast<int>(handle_)) != 0)
        return lastError("core.file_flush", "flush failed");
    return {};
}

Result<void> File::setInheritable(bool inheritable) {
    const int fd = static_cast<int>(handle_);
    int flags = ::fcntl(fd, F_GETFD);
    if (flags < 0)
        return lastError("core.file_inherit", "fcntl failed");
    flags = inheritable ? (flags & ~FD_CLOEXEC) : (flags | FD_CLOEXEC);
    if (::fcntl(fd, F_SETFD, flags) != 0)
        return lastError("core.file_inherit", "fcntl failed");
    return {};
}

void File::close() {
    if (handle_ != kInvalid) {
        ::close(static_cast<int>(handle_));
        handle_ = kInvalid;
    }
}

MappedFile::~MappedFile() {
    reset();
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : view_(std::exchange(other.view_, nullptr)), size_(std::exchange(other.size_, 0)),
      mapping_(std::exchange(other.mapping_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        reset();
        view_ = std::exchange(other.view_, nullptr);
        size_ = std::exchange(other.size_, 0);
        mapping_ = std::exchange(other.mapping_, 0);
    }
    return *this;
}

void MappedFile::reset() {
    if (view_)
        ::munmap(const_cast<void*>(view_), size_);
    view_ = nullptr;
    size_ = 0;
}

Result<MappedFile> MappedFile::map(const File& file) {
    auto size = file.size();
    if (!size)
        return size.error();
    MappedFile m;
    if (*size == 0)
        return m;
    void* view = ::mmap(nullptr, static_cast<std::size_t>(*size), PROT_READ, MAP_PRIVATE,
                        static_cast<int>(file.nativeHandle()), 0);
    if (view == MAP_FAILED)
        return lastError("core.mmap", "mmap failed");
    m.view_ = view;
    m.size_ = static_cast<std::size_t>(*size);
    return m;
}

#endif

Result<void> File::readExactAt(std::uint64_t offset, std::span<std::uint8_t> out) const {
    auto n = readAt(offset, out);
    if (!n)
        return n.error();
    if (*n != out.size())
        return makeError("core.file_short_read", "unexpected end of file");
    return {};
}

Result<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& path, std::size_t maxBytes) {
    auto f = File::open(path, File::Mode::Read);
    if (!f)
        return f.error();
    auto size = f->size();
    if (!size)
        return size.error();
    if (*size > maxBytes)
        return makeError("core.file_too_large", "file exceeds the size limit");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(*size));
    GMDR_TRY(f->readExactAt(0, data));
    return data;
}

} // namespace gmdr
