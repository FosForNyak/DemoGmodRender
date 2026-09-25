// =============================================================================
//  icon_item.hpp — векторні значки інтерфейсу (Icon у QML) і логотип (Logo).
//
//  Ті самі значки, що й у старому вікні (малюються лініями, а не з файлів), тож
//  чіткі за будь-якого масштабу і не потребують модуля SVG. Назва — name
//  ("play", "folder", "warning" ...), колір — color.
// =============================================================================
#pragma once

#include <QColor>
#include <QQuickPaintedItem>

namespace gmdr::qt {

class IconItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit IconItem(QQuickItem* parent = nullptr);
    void paint(QPainter* p) override;

    QString name() const { return name_; }
    void    setName(const QString& n);
    QColor  color() const { return color_; }
    void    setColor(const QColor& c);

signals:
    void nameChanged();
    void colorChanged();

private:
    QString name_;
    QColor  color_ = Qt::white;
};

class LogoItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)

public:
    explicit LogoItem(QQuickItem* parent = nullptr);
    void   paint(QPainter* p) override;
    QColor accent() const { return accent_; }
    void   setAccent(const QColor& c);

signals:
    void accentChanged();

private:
    QColor accent_ = QColor(0x4C, 0xC2, 0xFF);
};

} // namespace gmdr::qt
