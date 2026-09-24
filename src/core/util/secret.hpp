// =============================================================================
//  secret.hpp — API-ключі сервісів (переклад, озвучення) у файлі налаштувань.
//
//  У Windows ключ шифрується DPAPI (CryptProtectData) — розшифрувати його може
//  лише той самий користувач на тому ж ПК; у файлі — "dpapi:<base64>". На інших
//  ОС — "plain:<ключ>" (програма там лише для розробки). Порожній ключ — порожній рядок.
// =============================================================================
#pragma once

#include <string>

namespace gmdr {

std::string protect_secret(const std::string& plain);
// Порожньо, якщо рядок порожній або не розшифровується (інший користувач чи ПК).
std::string unprotect_secret(const std::string& stored);

// Base64 (стандартний алфавіт, з вирівнюванням '=').
std::string base64_encode(const std::string& bytes);
std::string base64_decode(const std::string& text);

} // namespace gmdr
