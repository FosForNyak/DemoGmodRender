#include "core/text.h"

#include <cstdint>

namespace gmdr {

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string sanitizeUtf8(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    const auto* s = reinterpret_cast<const unsigned char*>(in.data());
    const std::size_t n = in.size();
    std::size_t i = 0;
    auto replacement = [&] { out += "\xEF\xBF\xBD"; };
    while (i < n) {
        const unsigned char c = s[i];
        if (c < 0x80) {
            // Control characters other than tab/newline are replaced too: they break logs and UI.
            if (c < 0x20 && c != '\t' && c != '\n')
                replacement();
            else
                out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        int len = 0;
        std::uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            replacement();
            ++i;
            continue;
        }
        if (i + static_cast<std::size_t>(len) > n) {
            replacement();
            ++i;
            continue;
        }
        bool valid = true;
        for (int k = 1; k < len; ++k) {
            const unsigned char cc = s[i + static_cast<std::size_t>(k)];
            if ((cc & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Reject overlong forms, surrogates and out-of-range code points.
        if (valid) {
            if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
                (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
                valid = false;
        }
        if (!valid) {
            replacement();
            ++i;
            continue;
        }
        out.append(in.substr(i, static_cast<std::size_t>(len)));
        i += static_cast<std::size_t>(len);
    }
    return out;
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string normalizeGamePath(std::string_view s) {
    std::string out = toLowerAscii(s);
    for (auto& c : out)
        if (c == '\\')
            c = '/';
    std::string collapsed;
    collapsed.reserve(out.size());
    for (char c : out) {
        if (c == '/' && !collapsed.empty() && collapsed.back() == '/')
            continue;
        collapsed.push_back(c);
    }
    std::string_view v = collapsed;
    while (startsWith(v, "./"))
        v.remove_prefix(2);
    while (startsWith(v, "/"))
        v.remove_prefix(1);
    return std::string(v);
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace gmdr
