// =============================================================================
//  file_assoc.hpp — відкривати .dem подвійним кліком (Windows, лише поточний користувач).
//
//  Реєструється власний тип "GModDemoRender.dem" з командою відкриття і програма в
//  списку «Відкрити за допомогою» для .dem. Типовою програмою для .dem вона стає, лише
//  якщо інша ще не призначена (чужий вибір не перебиваємо); Windows 10/11 може все одно
//  спитати, чим відкривати, — це вирішує сам користувач. Лише HKCU, без прав адміністратора;
//  вмикається і вимикається в «Інструментах».
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gmdr {

constexpr const char* kDemProgId = "GModDemoRender.dem";

// Що саме пишеться (ключі — відносно classes_root); для тестів і показу користувачу.
struct RegValue {
    std::string key;
    std::string name;    // порожньо — значення за замовчуванням
    std::string value;
};
std::vector<RegValue> dem_association_values(const std::filesystem::path& exe);

// classes_root — розділ у HKEY_CURRENT_USER (тести пишуть в окремий, не в справжній Classes).
bool register_dem_association(const std::filesystem::path& exe, std::string* error,
                              const std::string& classes_root = "Software\\Classes");
bool unregister_dem_association(std::string* error, const std::string& classes_root = "Software\\Classes");
bool dem_association_registered(const std::filesystem::path& exe,
                                const std::string& classes_root = "Software\\Classes");

} // namespace gmdr
