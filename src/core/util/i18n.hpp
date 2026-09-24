// =============================================================================
//  i18n.hpp — мова інтерфейсу: українська (вихідна), англійська і ще 19 мов.
//
//  Текст у коді лишається українським і водночас є ключем перекладу (як у gettext):
//  tr("Почати рендер") повертає "Start render", якщо вибрано English, інакше — сам
//  рядок. Переклади — у i18n/<мова>.inc. Чого в мові ще немає, показується англійською.
//  Мова обирається при запуску (налаштування ui_language або GMDR_LANG) і не
//  змінюється до перезапуску.
//
//  ImGui-мітки з "##id" перекладаються до "##", ідентифікатор лишається тим самим.
//  Форматні рядки — trf("Кадрів: {}", n): переклад має ті самі {} у тому ж порядку.
// =============================================================================
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr {

struct UiLanguage {
    const char* code;      // "uk", "en", "pt-BR" …
    const char* native;    // назва мовою самої мови — щоб знайти свою
    const char* english;
};
// Усі мови інтерфейсу: українська, англійська, далі решта
const std::vector<UiLanguage>& ui_languages();

// Код мови з ui_languages() або "" — за мовою системи.
void        set_ui_language(const std::string& code);
std::string ui_language();   // поточна мова ("uk", "en", "de" …)
// Мова системи, якщо програма її знає (інакше "en"); для першого запуску.
std::string system_ui_language();

const char* tr(const char* uk);
inline std::string tr(const std::string& uk) { return tr(uk.c_str()); }
// Переклад іншою мовою, ніж поточна (напр. «мова зміниться після перезапуску» — вибраною мовою)
const char* tr_lang(const std::string& code, const char* uk);

template <class... A>
std::string trf(const char* uk, const A&... a) {
    return std::vformat(tr(uk), std::make_format_args(a...));
}

// Кількість перекладів поточної мови (англійської, якщо вибрано українську) — для тестів і журналу.
size_t translation_count();

} // namespace gmdr

// Позначити рядок для перекладу там, де tr() викликати ще рано (таблиці, статичні масиви):
// N_("Огляд") — це сам український рядок, а перекладає його tr() на місці показу.
#define N_(s) s
