#include "translator.hpp"

#include "core/util/i18n.hpp"

namespace gmdr::qt {

QString CoreTranslator::translate(const char*, const char* source, const char*, int) const {
    if (!source || !*source) return {};
    return QString::fromUtf8(gmdr::tr(source));
}

} // namespace gmdr::qt
