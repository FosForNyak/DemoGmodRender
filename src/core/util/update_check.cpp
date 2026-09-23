#include "update_check.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#include "json.hpp"
#include "strings.hpp"
#include "i18n.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#ifndef GMDR_VERSION
#define GMDR_VERSION "0"
#endif

namespace gmdr {

namespace {
std::vector<int> version_parts(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<int> out;
    int cur = -1;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        } else if (c == '.') {
            out.push_back(std::max(0, cur));
            cur = -1;
        } else {
            break;   // "1.3.0-beta" — суфікс не враховуємо
        }
    }
    if (cur >= 0) out.push_back(cur);
    while (out.size() > 1 && out.back() == 0) out.pop_back();   // 1.2 == 1.2.0
    return out;
}
} // namespace

int compare_versions(const std::string& a, const std::string& b) {
    const auto pa = version_parts(a), pb = version_parts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::optional<ReleaseInfo> parse_latest_release(const std::string& text, std::string* error) {
    auto j = json::parse(text);
    if (!j || !j->is_object()) {
        if (error) *error = tr("незрозуміла відповідь GitHub");
        return std::nullopt;
    }
    ReleaseInfo r;
    r.version = (*j)["tag_name"].as_string();
    if (!r.version.empty() && (r.version[0] == 'v' || r.version[0] == 'V')) r.version.erase(0, 1);
    r.url = (*j)["html_url"].as_string();
    r.published = (*j)["published_at"].as_string().substr(0, 10);
    // Опис релізу: кілька перших непорожніх рядків
    int lines = 0;
    for (const auto& line : split(replace_all((*j)["body"].as_string(), "\r", ""), '\n')) {
        if (trim(line).empty()) continue;
        r.notes += trim(line) + "\n";
        if (++lines == 8) break;
    }
    if (r.version.empty()) {
        if (error) *error = tr("у відповіді GitHub немає номера версії");
        return std::nullopt;
    }
    return r;
}

#ifdef _WIN32
std::optional<ReleaseInfo> fetch_latest_release(const std::string& repo, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<ReleaseInfo> {
        if (error) *error = msg;
        return std::nullopt;
    };
    const std::wstring agent = utf8_to_wide(std::string("GModDemoRender/") + GMDR_VERSION);
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return fail(tr("не вдалося почати з'єднання"));
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 15000);
    HINTERNET conn = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    const std::wstring path = utf8_to_wide("/repos/" + repo + "/releases/latest");
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                         : nullptr;
    std::string body;
    DWORD status = 0;
    bool ok = req != nullptr;
    if (ok) {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        ok = WinHttpSendRequest(req, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
             WinHttpReceiveResponse(req, nullptr);
    }
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < (4u << 20);) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
            body.append(chunk.data(), read);
        }
    }
    const DWORD net_err = ok ? 0 : GetLastError();
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    if (!ok) return fail(tr("немає зв'язку з GitHub (код ") + std::to_string(net_err) + ")");
    if (status == 404) return fail(tr("на GitHub ще немає опублікованих версій (або репозиторій закритий)"));
    if (status == 403 || status == 429) return fail(tr("GitHub тимчасово обмежив запити — спробуйте пізніше"));
    if (status != 200) return fail(tr("GitHub відповів кодом ") + std::to_string(status));
    return parse_latest_release(body, error);
}
#else
std::optional<ReleaseInfo> fetch_latest_release(const std::string&, std::string* error) {
    if (error) *error = tr("перевірка оновлень поки що лише у Windows — дивіться сторінку релізів на GitHub");
    return std::nullopt;
}
#endif

} // namespace gmdr
