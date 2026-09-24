#include "http.hpp"

#include "i18n.hpp"
#include "json.hpp"
#include "strings.hpp"

#include <format>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace gmdr {

std::string multipart_body(const std::vector<FormPart>& parts, const std::string& boundary) {
    std::string b;
    for (const auto& p : parts) {
        b += "--" + boundary + "\r\n";
        b += "Content-Disposition: form-data; name=\"" + p.name + "\"";
        if (!p.filename.empty()) b += "; filename=\"" + p.filename + "\"";
        b += "\r\n";
        if (!p.filename.empty()) b += "Content-Type: " + (p.content_type.empty() ? std::string("application/octet-stream") : p.content_type) + "\r\n";
        b += "\r\n";
        b += p.data;
        b += "\r\n";
    }
    b += "--" + boundary + "--\r\n";
    return b;
}

std::string describe_http_error(const HttpResponse& r) {
    if (r.status == 0) return r.error.empty() ? std::string(tr("немає зв'язку з сервісом")) : r.error;
    std::string msg;
    if (auto j = json::parse(r.body)) {
        // Різні сервіси кладуть пояснення по-різному
        const json::Value& v = *j;
        for (const char* k : {"message", "error", "detail"}) {
            const json::Value& f = v[k];
            if (f.type() == json::Value::Type::String) msg = f.as_string();
            else if (f.is_object()) {
                if (f["message"].type() == json::Value::Type::String) msg = f["message"].as_string();
                else if (f["status"].type() == json::Value::Type::String) msg = f["status"].as_string();
            }
            if (!msg.empty()) break;
        }
    }
    if (msg.empty()) msg = trim(r.body.substr(0, 200));
    return trf("код {}{}", r.status, msg.empty() ? "" : ": " + msg);
}

#ifdef _WIN32
HttpResponse http_request(const HttpRequest& r, const std::atomic<bool>* cancel) {
    HttpResponse res;
    const std::wstring wurl = utf8_to_wide(r.url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {}, extra[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP)) {
        res.error = tr("неправильна адреса: ") + r.url;
        return res;
    }
    const std::wstring target = std::wstring(path) + extra;   // шлях + ?параметри
    const std::wstring agent = utf8_to_wide(std::string("GModDemoRender/") + GMDR_VERSION);
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = tr("не вдалося почати з'єднання");
        return res;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 60000, r.timeout_ms);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, utf8_to_wide(r.method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                         : nullptr;
    std::wstring headers;
    for (const auto& [k, v] : r.headers) headers += utf8_to_wide(k + ": " + v + "\r\n");
    bool ok = req && WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                        headers.empty() ? 0 : static_cast<DWORD>(-1),
                                        r.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(r.body.data()),
                                        static_cast<DWORD>(r.body.size()), static_cast<DWORD>(r.body.size()), 0) &&
              WinHttpReceiveResponse(req, nullptr);
    const DWORD net_err = ok ? 0 : GetLastError();
    if (ok) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        res.status = static_cast<int>(status);
        wchar_t ctype[256] = {};
        size = sizeof(ctype);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ctype, &size, WINHTTP_NO_HEADER_INDEX))
            res.content_type = wide_to_utf8(ctype);
        for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail > 0;) {
            if (cancel && cancel->load()) {
                res.status = 0;
                res.error = tr("скасовано");
                break;
            }
            const size_t at = res.body.size();
            res.body.resize(at + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, res.body.data() + at, avail, &read) || read == 0) {
                res.body.resize(at);
                break;
            }
            res.body.resize(at + read);
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    if (!ok) {
        res.status = 0;
        res.error = net_err == ERROR_WINHTTP_TIMEOUT ? trf("{} не відповів вчасно", wide_to_utf8(host))
                    : net_err == ERROR_WINHTTP_CANNOT_CONNECT ? trf("не вдалося з'єднатися з {} (сервер не запущено?)", wide_to_utf8(host))
                                                              : trf("немає зв'язку з {} (код {})", wide_to_utf8(host), net_err);
    }
    return res;
}
#else
HttpResponse http_request(const HttpRequest&, const std::atomic<bool>*) {
    HttpResponse res;
    res.error = tr("запити до сервісів перекладу й озвучення поки що лише у Windows");
    return res;
}
#endif

} // namespace gmdr
