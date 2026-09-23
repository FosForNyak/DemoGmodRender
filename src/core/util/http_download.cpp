#include "http_download.hpp"

#include "file_util.hpp"
#include "strings.hpp"

#include <format>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace gmdr {

namespace fs = std::filesystem;

#ifdef _WIN32
bool download_file(const std::string& url, const fs::path& dest, const std::function<void(uint64_t, uint64_t)>& progress,
                   const std::atomic<bool>* cancel, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    const std::wstring wurl = utf8_to_wide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
        return fail("неправильна адреса (потрібна https://): " + url);
    const std::wstring agent = utf8_to_wide(std::string("GModDemoRender/") + GMDR_VERSION);
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return fail("не вдалося почати з'єднання");
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    // Переходи (Hugging Face віддає файл з CDN) WinHTTP робить сам
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE)
                         : nullptr;
    bool ok = req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(req, nullptr);
    const DWORD net_err = ok ? 0 : GetLastError();
    DWORD status = 0;
    uint64_t total = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        wchar_t len[32] = {};
        size = sizeof(len);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &size,
                                WINHTTP_NO_HEADER_INDEX))
            total = std::wcstoull(len, nullptr, 10);
    }
    auto close_all = [&] {
        if (req) WinHttpCloseHandle(req);
        if (conn) WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
    };
    if (!ok) {
        close_all();
        return fail(std::format("немає зв'язку з {} (код {})", wide_to_utf8(host), net_err));
    }
    if (status != 200) {
        close_all();
        return fail(std::format("сервер відповів кодом {}", status));
    }
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    const fs::path part = fs::path(dest).concat(".part");
    std::ofstream f(part, std::ios::binary | std::ios::trunc);
    if (!f) {
        close_all();
        return fail("не вдалося створити " + path_to_utf8(part));
    }
    std::vector<char> buf(1 << 20);
    uint64_t done = 0;
    std::string err;
    for (;;) {
        if (cancel && cancel->load()) {
            err = "скасовано";
            break;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            err = std::format("з'єднання обірвалось (код {})", GetLastError());
            break;
        }
        if (avail == 0) break;   // кінець
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), std::min<DWORD>(avail, static_cast<DWORD>(buf.size())), &got) || got == 0) {
            err = std::format("з'єднання обірвалось (код {})", GetLastError());
            break;
        }
        if (!f.write(buf.data(), got)) {
            err = "не вдалося записати файл (місце на диску?)";
            break;
        }
        done += got;
        if (progress) progress(done, total);
    }
    f.close();
    close_all();
    if (err.empty() && total > 0 && done != total) err = std::format("отримано {} з {} байтів", done, total);
    if (!err.empty()) {
        fs::remove(part, ec);
        return fail(err);
    }
    fs::rename(part, dest, ec);
    if (ec) {
        fs::remove(part, ec);
        return fail("не вдалося перейменувати файл: " + ec.message());
    }
    return true;
}
#else
bool download_file(const std::string&, const fs::path&, const std::function<void(uint64_t, uint64_t)>&,
                   const std::atomic<bool>*, std::string* error) {
    if (error) *error = "завантаження з програми є лише у Windows — завантажте файл вручну";
    return false;
}
#endif

} // namespace gmdr
