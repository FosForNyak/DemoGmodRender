// =============================================================================
//  translator.hpp — переклад рядків QML тими самими каталогами, що й ядро.
//
//  У QML текст пишеться як qsTr("Український текст") — це ключ перекладу (як tr() у
//  C++); CoreTranslator віддає переклад з src/core/util/i18n/<мова>.inc. Так увесь
//  інтерфейс має один набір перекладів, а scripts/i18n.py перевіряє і QML.
// =============================================================================
#pragma once

#include <QTranslator>

namespace gmdr::qt {

class CoreTranslator final : public QTranslator {
public:
    using QTranslator::QTranslator;
    QString translate(const char* context, const char* source, const char* disambiguation, int n) const override;
    bool    isEmpty() const override { return false; }
};

} // namespace gmdr::qt
