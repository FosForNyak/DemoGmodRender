// =============================================================================
//  graphics_api.hpp — графічний API самого вікна програми (Qt Quick): Direct3D 11/12,
//  Vulkan, OpenGL, програмне малювання. Це не рендерер гри (див. game_renderer.hpp).
//
//  * platform_graphics_apis() — що взагалі можна вибрати на цій ОС і в цій збірці Qt
//    (Metal не показується у Windows, Direct3D — у Linux);
//  * probe_graphics_apis() — чи API справді ініціалізується (створюється пристрій),
//    а не лише чи є його DLL;
//  * apply_graphics_api() — до створення вікна (QQuickWindow::setGraphicsApi);
//  * захист від збою: перед першим кадром з вибраним API пишеться позначка; якщо
//    програма не дійшла до кадру (впала, зависла), наступний запуск повертає
//    «Автоматично» і каже, що сталося.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/config/capabilities.hpp"

class QQuickWindow;

namespace gmdr::qt {

// Усі варіанти для цієї ОС і збірки Qt (стан — Unknown, поки не перевірено)
std::vector<config::GraphicsApiInfo> platform_graphics_apis();
// Перевірка ініціалізації кожного варіанту. Потрібен QGuiApplication; повільно (сотні мс).
std::vector<config::GraphicsApiInfo> probe_graphics_apis();

// Вибрати API до створення вікна. Невідомий чи недоступний у цій збірці — «Автоматично».
// Повертає id, який справді застосовано.
std::string apply_graphics_api(const std::string& id);
// Яким API вікно малює зараз ("d3d11", "vulkan" ...; порожньо — ще не відомо)
std::string active_graphics_api(QQuickWindow* window);
std::string graphics_api_label(const std::string& id);

// Захист від збою ініціалізації: API, з яким минулий запуск не дійшов до першого кадру (або "")
std::string take_failed_graphics_api();
void        mark_graphics_pending(const std::string& id);   // перед створенням вікна
void        mark_graphics_ok();                            // перший кадр показано

} // namespace gmdr::qt
