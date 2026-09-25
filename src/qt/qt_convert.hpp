// =============================================================================
//  qt_convert.hpp — перетворення між типами ядра (UTF-8 рядки, json::Value) і Qt
//  (QString, QVariant) для шару інтерфейсу.
// =============================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <string>
#include <vector>

#include "core/config/settings_catalog.hpp"
#include "core/util/json.hpp"

namespace gmdr::qt {

inline QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
inline QString qs(const char* s) { return QString::fromUtf8(s ? s : ""); }
inline std::string ss(const QString& s) { return s.toUtf8().toStdString(); }

inline QStringList qs_list(const std::vector<std::string>& v) {
    QStringList out;
    for (const auto& s : v) out << qs(s);
    return out;
}

inline QVariant to_variant(const json::Value& v) {
    switch (v.type()) {
    case json::Value::Type::Bool: return v.as_bool();
    case json::Value::Type::Number: return v.as_number();
    case json::Value::Type::String: return qs(v.as_string());
    default: return {};
    }
}

// Значення з QML у json::Value потрібного типу (типу поля налаштувань)
inline json::Value to_json(const QVariant& v, config::SettingType type) {
    switch (type) {
    case config::SettingType::Bool: return json::Value::boolean(v.toBool());
    case config::SettingType::Int:
    case config::SettingType::Double: {
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok ? json::Value::number(d) : json::Value();
    }
    default: return json::Value::string(ss(v.toString()));
    }
}

} // namespace gmdr::qt
