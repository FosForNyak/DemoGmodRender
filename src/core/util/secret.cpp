#include "secret.hpp"

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace gmdr {

namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64_encode(const std::string& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) | static_cast<uint8_t>(bytes[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    if (i < bytes.size()) {
        uint32_t v = static_cast<uint8_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= static_cast<uint8_t>(bytes[i + 1]) << 8;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string base64_decode(const std::string& text) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;   // '=' і пробіли
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

std::string protect_secret(const std::string& plain) {
    if (plain.empty()) return {};
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (CryptProtectData(&in, L"GModDemoRender", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        std::string blob(reinterpret_cast<const char*>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return "dpapi:" + base64_encode(blob);
    }
#endif
    return "plain:" + plain;
}

std::string unprotect_secret(const std::string& stored) {
    if (stored.rfind("plain:", 0) == 0) return stored.substr(6);
#ifdef _WIN32
    if (stored.rfind("dpapi:", 0) == 0) {
        const std::string blob = base64_decode(stored.substr(6));
        DATA_BLOB in{static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(const_cast<char*>(blob.data()))};
        DATA_BLOB out{};
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            std::string plain(reinterpret_cast<const char*>(out.pbData), out.cbData);
            SecureZeroMemory(out.pbData, out.cbData);
            LocalFree(out.pbData);
            return plain;
        }
    }
#endif
    return {};
}

} // namespace gmdr
