#include "mapped_file.hpp"
#include "i18n.hpp"

#include <format>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gmdr {

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& o) noexcept { *this = std::move(o); }

MappedFile& MappedFile::operator=(MappedFile&& o) noexcept {
    if (this == &o) return *this;
    close();
    view_ = std::exchange(o.view_, nullptr);
    size_ = std::exchange(o.size_, 0);
#ifdef _WIN32
    file_ = std::exchange(o.file_, nullptr);
    mapping_ = std::exchange(o.mapping_, nullptr);
#else
    fd_ = std::exchange(o.fd_, -1);
#endif
    return *this;
}

#ifdef _WIN32
bool MappedFile::open(const std::filesystem::path& path, std::string* error) {
    close();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        if (error) *error = trf("не вдалося відкрити файл (код {})", GetLastError());
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 ||
        static_cast<unsigned long long>(sz.QuadPart) > static_cast<unsigned long long>(SIZE_MAX)) {
        CloseHandle(f);
        if (error) *error = tr("порожній або завеликий файл");
        return false;
    }
    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) {
        if (error) *error = trf("не вдалося відобразити файл у пам'ять (код {})", GetLastError());
        CloseHandle(f);
        return false;
    }
    void* v = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!v) {
        if (error) *error = trf("не вдалося відобразити файл у пам'ять (код {})", GetLastError());
        CloseHandle(m);
        CloseHandle(f);
        return false;
    }
    file_ = f;
    mapping_ = m;
    view_ = v;
    size_ = static_cast<size_t>(sz.QuadPart);
    return true;
}

void MappedFile::close() {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_) CloseHandle(static_cast<HANDLE>(file_));
    view_ = mapping_ = file_ = nullptr;
    size_ = 0;
}
#else
bool MappedFile::open(const std::filesystem::path& path, std::string* error) {
    close();
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (error) *error = tr("не вдалося відкрити файл");
        return false;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        if (error) *error = tr("порожній файл");
        return false;
    }
    void* v = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (v == MAP_FAILED) {
        ::close(fd);
        if (error) *error = tr("не вдалося відобразити файл у пам'ять");
        return false;
    }
    madvise(v, static_cast<size_t>(st.st_size), MADV_SEQUENTIAL);
    fd_ = fd;
    view_ = v;
    size_ = static_cast<size_t>(st.st_size);
    return true;
}

void MappedFile::close() {
    if (view_) munmap(view_, size_);
    if (fd_ >= 0) ::close(fd_);
    view_ = nullptr;
    fd_ = -1;
    size_ = 0;
}
#endif

} // namespace gmdr
