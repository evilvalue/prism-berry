// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryTheme.h"

#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

namespace berry::theme {

namespace {
QString g_family;
QString g_familySemi;

QString registerFont(const QString& path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return {};
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.first();
}

void plot(QPainter& painter, int x, int y, int scale, const QColor& color)
{
    painter.fillRect(QRect(x * scale, y * scale, scale, scale), color);
}
}  // namespace

void init()
{
    if (!g_family.isEmpty())
        return;

    g_family = registerFont(QStringLiteral(":/berry/MinecraftRus.ttf"));
    g_familySemi = g_family;
    if (g_family.isEmpty())
        g_family = QStringLiteral("Consolas");
    if (g_familySemi.isEmpty())
        g_familySemi = g_family;
}

QString family()
{
    init();
    return g_family;
}

QFont regular(int pixelSize)
{
    init();
    QFont font(g_family);
    font.setPixelSize(pixelSize);
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QFont semiBold(int pixelSize)
{
    init();
    QFont font(g_familySemi);
    font.setPixelSize(pixelSize);
    font.setBold(true);
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QFont spaced(int pixelSize, qreal spacing, bool bold)
{
    init();
    QFont font(g_familySemi);
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QColor mix(const QColor& a, const QColor& b, int percent)
{
    const qreal t = qBound(0, percent, 100) / 100.0;
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

QPixmap pixelPixmap(const QStringList& glyph, const QColor& color, int scale)
{
    int columns = 0;
    for (const QString& row : glyph)
        columns = qMax(columns, int(row.size()));
    if (columns == 0 || glyph.isEmpty() || scale <= 0)
        return {};

    // One empty pixel all around for the hard outline; keeps the silhouette
    // readable on every background without antialiasing.
    const QSize size((columns + 2) * scale, (glyph.size() + 2) * scale);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    const auto filled = [&](int x, int y) {
        if (y < 0 || y >= glyph.size())
            return false;
        const QString& line = glyph.at(y);
        return x >= 0 && x < line.size() && line.at(x) == QLatin1Char('#');
    };

    for (int y = 0; y < glyph.size(); ++y) {
        for (int x = 0; x < columns; ++x) {
            if (!filled(x, y))
                continue;
            if (!filled(x - 1, y) || !filled(x + 1, y) || !filled(x, y - 1) || !filled(x, y + 1))
                plot(painter, x + 1, y + 1, scale, Ink);
        }
    }
    for (int y = 0; y < glyph.size(); ++y) {
        for (int x = 0; x < columns; ++x) {
            if (filled(x, y))
                plot(painter, x + 1, y + 1, scale, color);
        }
    }
    return pixmap;
}

QPixmap pixelArt(const QStringList& rows, const QColor& body, int scale)
{
    int columns = 0;
    for (const QString& row : rows)
        columns = qMax(columns, int(row.size()));
    if (columns == 0 || rows.isEmpty() || scale <= 0)
        return {};

    QPixmap pixmap(columns * scale, rows.size() * scale);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);

    for (int y = 0; y < rows.size(); ++y) {
        const QString& line = rows.at(y);
        for (int x = 0; x < line.size(); ++x) {
            switch (line.at(x).toLatin1()) {
                case '#':
                    plot(painter, x, y, scale, body);
                    break;
                case '+':
                    plot(painter, x, y, scale, mix(body, QColor(Qt::white), 55));
                    break;
                case '-':
                    plot(painter, x, y, scale, mix(body, QColor(Qt::black), 40));
                    break;
                case 'o':
                    plot(painter, x, y, scale, Ink);
                    break;
                case 'g':
                    plot(painter, x, y, scale, QColor(0x5B, 0xD1, 0x6B));
                    break;
                case 'G':
                    plot(painter, x, y, scale, QColor(0x2C, 0x7A, 0x3A));
                    break;
                default:
                    break;
            }
        }
    }
    return pixmap;
}

QIcon pixelIcon(const QStringList& glyph, const QColor& color)
{
    QIcon icon;
    icon.addPixmap(pixelPixmap(glyph, color, 1));
    icon.addPixmap(pixelPixmap(glyph, color, 2));
    icon.addPixmap(pixelPixmap(glyph, color, 3));
    return icon;
}

void bevel(QPainter& painter, const QRect& rect, const QColor& fill, const QColor& border, const QColor& highlight,
           const QColor& shadow)
{
    if (rect.width() <= 0 || rect.height() <= 0)
        return;

    painter.fillRect(rect, border);
    const QRect inner = rect.adjusted(2, 2, -2, -2);
    if (inner.width() <= 0 || inner.height() <= 0)
        return;

    painter.fillRect(inner, fill);
    painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), 1), highlight);
    painter.fillRect(QRect(inner.left(), inner.top(), 1, inner.height()), highlight);
    painter.fillRect(QRect(inner.left(), inner.bottom(), inner.width(), 1), shadow);
    painter.fillRect(QRect(inner.right(), inner.top(), 1, inner.height()), shadow);
}

void inset(QPainter& painter, const QRect& rect, const QColor& fill, const QColor& border)
{
    painter.fillRect(rect, border);
    const QRect inner = rect.adjusted(2, 2, -2, -2);
    if (inner.width() <= 0 || inner.height() <= 0)
        return;
    painter.fillRect(inner, fill);
    painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), 1), mix(fill, Qt::black, 55));
    painter.fillRect(QRect(inner.left(), inner.top(), 1, inner.height()), mix(fill, Qt::black, 55));
}

void apply(QWidget* root)
{
    init();
    root->setFont(regular(12));
    root->setStyleSheet(QStringLiteral(R"(
/* ---------- base ---------- */
QWidget#berryRoot { background: #0A0D12; color: #F2F5FA; }
QStackedWidget#berryPages { background: transparent; }
QWidget#berryPage { background: transparent; }
QWidget { color: #F2F5FA; }
QLabel { background: transparent; color: #F2F5FA; }
QLabel:disabled { color: #4C5566; }

QLabel#berryDisplay { color: #F7FAFF; font-size: 38px; }
QLabel#berryTitle { color: #F7FAFF; font-size: 26px; }
QLabel#berryH1 { color: #F5F8FD; font-size: 16px; }
QLabel#berryH2 { color: #F5F8FD; font-size: 12px; }
QLabel#berryCaption { color: #7E8A9E; font-size: 10px; }
QLabel#berryMuted { color: #A7B2C4; font-size: 12px; }
QLabel#berryDim { color: #77839A; font-size: 11px; }
QLabel#berryMono { color: #B9C4D6; font-size: 11px; }
QLabel#berryAccent { color: #FF5D86; }
QLabel#berryOk { color: #52E39C; }
QLabel#berryWarn { color: #FFC24B; }
QLabel#berryErr { color: #FF6B6B; }

/* ---------- top bar / tabs / footer ---------- */
QFrame#berryTopbar { background: #0C1016; border: none; border-bottom: 2px solid #1A212B; }
QFrame#berryTopbarLine { background: #FF4F79; border: none; min-height: 2px; max-height: 2px; }
QWidget#berryTabStrip { background: #080B0F; border-bottom: 2px solid #1A212B; }
QFrame#berryFooter { background: #0C1016; border: none; border-top: 2px solid #1A212B; }
QLabel#berryFooterCell { color: #8C98AC; font-size: 10px; padding: 0px 10px; }

/* ---------- text inputs ---------- */
QLineEdit, QSpinBox {
    background: #07090D;
    border: 2px solid #333D4E;
    color: #F2F5FA;
    padding: 7px 9px;
    min-height: 20px;
    selection-background-color: #FF4F79;
    selection-color: #14070C;
}
QLineEdit:focus, QSpinBox:focus { border-color: #FF4F79; }
QLineEdit:disabled, QSpinBox:disabled { color: #4C5566; border-color: #262E3C; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; height: 0; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0; height: 0; }

/* ---------- scrolling ---------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #07090D; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #2A3342; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #FF4F79; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: #07090D; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #2A3342; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ---------- lists ---------- */
QListWidget {
    background: #07090D;
    border: 2px solid #333D4E;
    color: #F2F5FA;
    outline: none;
}
QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #141A23; }
QListWidget::item:hover { background: #151A22; }
QListWidget::item:selected { background: #2A1020; color: #FFD7E1; }

/* ---------- tooltips and menus ---------- */
QToolTip { background: #151A22; color: #F2F5FA; border: 2px solid #FF4F79; padding: 4px 6px; }
QMenu { background: #151A22; border: 2px solid #333D4E; }
QMenu::item { padding: 5px 18px; }
QMenu::item:selected { background: #FF4F79; color: #14070C; }
)")
    );
}

}  // namespace berry::theme
