// =============================================================================
//  constraints_internal.hpp — спільне для рушія перевірки (constraints.cpp) і
//  правил (rules.cpp). Поза модулем config не використовується.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "constraints.hpp"

namespace gmdr::config::detail {

// Усе, що бачить правило: налаштування, середовище, мету перевірки і результат, куди писати
struct RuleContext {
    const render::RenderSettings&  s;
    const EnvironmentCapabilities& env;
    const ValidationContext&       ctx;
    ValidationResult&              r;
    std::string                    rule;   // ідентифікатор поточного правила

    const Derived& d() const { return r.derived; }
    // Перевірка перед виконанням (рендер, тест, черга), а не під час редагування
    bool executing() const { return ctx.purpose != ValidationContext::Purpose::Edit; }
    SettingState& state(SettingId id) { return r.states[static_cast<size_t>(id)]; }
    Issue& add(Severity sev, IssueKind kind, SettingId setting, std::string message, std::string explanation = {},
               std::vector<SettingId> related = {});
    // Налаштування не стосується (сховати або вимкнути з поясненням)
    void hide(SettingId id, const std::string& why);
    void disable(SettingId id, const std::string& why);
};

using RuleFn = void (*)(RuleContext&);
struct Rule {
    const char* id;
    const char* description;   // український ключ перекладу
    RuleFn      fn;
};
const std::vector<Rule>& rules();

Derived compute_derived(const render::RenderSettings& s, const EnvironmentCapabilities& env, const ValidationContext& ctx);

inline SettingChange change(SettingId id, json::Value v) { return {id, std::move(v)}; }
inline json::Value   str(std::string v) { return json::Value::string(std::move(v)); }
inline json::Value   num(double v) { return json::Value::number(v); }
inline json::Value   flag(bool v) { return json::Value::boolean(v); }
inline Fix           fix(std::string label, std::vector<SettingChange> changes) { return {std::move(label), std::move(changes)}; }

} // namespace gmdr::config::detail
