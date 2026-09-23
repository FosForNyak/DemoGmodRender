// =============================================================================
//  i18n.hpp — мова інтерфейсу: українська (вихідна) або англійська.
//
//  Текст у коді лишається українським і водночас є ключем перекладу (як у gettext):
//  tr("Почати рендер") повертає "Start render", якщо вибрано English, інакше — сам
//  рядок. Переклади — у i18n_en.inc. Мова обирається при запуску (налаштування
//  ui_language або GMDR_LANG) і не змінюється до перезапуску.
//
//  ImGui-мітки з "##id" перекладаються до "##", ідентифікатор лишається тим самим.
//  Форматні рядки — trf("Кадрів: {}", n): переклад має ті самі {} у тому ж порядку.
// =============================================================================
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace gmdr {

enum class UiLang { Uk, En };

// "uk", "en" або "" — за мовою системи (українська/російська Windows → uk, інакше en).
void   set_ui_language(const std::string& code);
UiLang ui_language();
// Мова системи Windows ("uk" / "en"), для першого запуску.
std::string system_ui_language();

const char* tr(const char* uk);
inline std::string tr(const std::string& uk) { return tr(uk.c_str()); }

template <class... A>
std::string trf(const char* uk, const A&... a) {
    return std::vformat(tr(uk), std::make_format_args(a...));
}

// Кількість перекладів (для тестів і журналу).
size_t translation_count();

} // namespace gmdr
