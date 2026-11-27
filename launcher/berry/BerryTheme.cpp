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
    font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

QPixmap pixelPixmap(const QStringList& glyph, const QColor& color, int scale)
{
    int columns = 0;
    for (const QString& row : glyph)
        columns = qMax(columns, int(row.size()));
    if (columns == 0 || glyph.isEmpty() || scale <= 0)
        return {};

    QPixmap pixmap(columns * scale, glyph.size() * scale);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (int row = 0; row < glyph.size(); ++row) {
        const QString& line = glyph.at(row);
        for (int column = 0; column < line.size(); ++column) {
            if (line.at(column) == QLatin1Char('#'))
                painter.fillRect(column * scale, row * scale, scale, scale, color);
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

void apply(QWidget* root)
{
    init();
    root->setFont(regular(12));
    root->setStyleSheet(QStringLiteral(R"(
/* ---------- base ---------- */
QWidget#berryRoot { background: #0B0D12; color: #F2F4F8; }
QStackedWidget#berryPages { background: #0B0D12; }
QWidget#berryPage { background: #0B0D12; }
QWidget { color: #F2F4F8; }
QLabel { background: transparent; color: #F2F4F8; }
QLabel:disabled { color: #5C6577; }

QLabel#berryDisplay { font-size: 34px; color: #F7F9FC; }
QLabel#berryTitle { font-size: 28px; color: #F7F9FC; }
QLabel#berryH1 { font-size: 17px; color: #F7F9FC; }
QLabel#berryH2 { font-size: 13px; color: #F7F9FC; }
QLabel#berryCaption { color: #8A94A6; font-size: 10px; }
QLabel#berryMuted { color: #A9B2C3; font-size: 12px; }
QLabel#berryDim { color: #7C8798; font-size: 11px; }
QLabel#berryMono { color: #A9B2C3; font-size: 11px; }
QLabel#berryAccent { color: #FF5D86; }
QLabel#berryOk { color: #4BE39F; }
QLabel#berryWarn { color: #F5B942; }
QLabel#berryErr { color: #FF6E7B; }
QLabel#berryStatus { background: #0E1219; border: 2px solid #20252F; border-left: 4px solid #2C3340; color: #A9B2C3; font-size: 11px; padding: 9px 11px; }

/* ---------- surfaces: a flat panel with a hard pixel shadow ---------- */
QFrame#berryCard { background: #141821; border: 2px solid #262C38; border-right: 4px solid #07080C; border-bottom: 4px solid #07080C; }
QFrame#berryHero { background: #141821; border: 2px solid #FF5D86; border-right: 4px solid #6E1B31; border-bottom: 4px solid #6E1B31; }
QFrame#berryShowcase { background: #0F131B; border: 2px solid #2C2333; border-right: 4px solid #07080C; border-bottom: 4px solid #07080C; }
QFrame#berryUpdate { background: #1B1720; border: 2px solid #FF5D86; border-right: 4px solid #6E1B31; border-bottom: 4px solid #6E1B31; }
QFrame#berryInset { background: #0E1219; border: 2px solid #20252F; }
QFrame#berryRule { background: #262C38; border: none; min-height: 2px; max-height: 2px; }
QFrame#berryRuleAccent { background: #FF5D86; border: none; min-height: 2px; max-height: 2px; }
QFrame#berryMark { background: #FF5D86; border: none; border-right: 2px solid #6E1B31; border-bottom: 2px solid #6E1B31; }
QFrame#berryGap { background: transparent; border: none; }

/* ---------- buttons ---------- */
QPushButton {
    background: #1B2029;
    border: 2px solid #3A4354;
    border-right: 4px solid #07080C;
    border-bottom: 4px solid #07080C;
    color: #F2F4F8;
    padding: 8px 14px;
    min-height: 18px;
}
QPushButton:hover { background: #232A36; border-color: #FF86A5; border-right-color: #07080C; border-bottom-color: #07080C; }
QPushButton:pressed { background: #12161E; border: 2px solid #8E2340; padding: 10px 12px 6px 16px; }
QPushButton:disabled { color: #5C6577; border-color: #222833; border-right-color: #07080C; border-bottom-color: #07080C; background: #10141C; }

QPushButton#berryPrimary { background: #FF5D86; border-color: #FF9DB8; border-right-color: #7A1E33; border-bottom-color: #7A1E33; color: #1A0B11; }
QPushButton#berryPrimary:hover { background: #FF86A5; border-color: #FFB3C6; border-right-color: #7A1E33; border-bottom-color: #7A1E33; }
QPushButton#berryPrimary:pressed { background: #E24A73; border: 2px solid #B62E52; padding: 10px 12px 6px 16px; }
QPushButton#berryPrimary:disabled { background: #2A1820; border-color: #3A2430; border-right-color: #07080C; border-bottom-color: #07080C; color: #7C5A66; }

QPushButton#berryPlay {
    background: #FF5D86;
    border: 2px solid #FF9DB8;
    border-right: 4px solid #7A1E33;
    border-bottom: 4px solid #7A1E33;
    color: #1A0B11;
    font-size: 19px;
    padding: 13px 22px;
}
QPushButton#berryPlay:hover { background: #FF86A5; border-color: #FFB3C6; }
QPushButton#berryPlay:pressed { background: #E24A73; border: 2px solid #B62E52; padding: 15px 20px 11px 24px; }
QPushButton#berryPlay:disabled { background: #2A1820; border-color: #3A2430; border-right-color: #07080C; border-bottom-color: #07080C; color: #7C5A66; }

QPushButton#berryGhost { background: transparent; border: 2px solid transparent; color: #98A3B5; padding: 6px 8px; }
QPushButton#berryGhost:hover { color: #FF86A5; border-color: #2C3340; background: #12161E; }
QPushButton#berryGhost:pressed { color: #FF5D86; border-color: #B62E52; background: #12161E; padding: 6px 8px; }
QPushButton#berryGhost:disabled { color: #4E5666; border-color: transparent; background: transparent; }

QPushButton#berrySegment { background: #0F131A; border: 2px solid #2C3340; border-right: 4px solid #07080C; border-bottom: 4px solid #07080C; color: #98A3B5; padding: 5px 12px; min-height: 16px; }
QPushButton#berrySegment:hover { color: #F2F4F8; border-color: #FF86A5; }
QPushButton#berrySegment:checked { background: #FF5D86; border-color: #FF9DB8; border-right-color: #7A1E33; border-bottom-color: #7A1E33; color: #1A0B11; }

QPushButton#berryIcon { padding: 0; min-width: 38px; max-width: 38px; min-height: 38px; max-height: 38px; }

/* ---------- inputs ---------- */
QLineEdit, QSpinBox {
    background: #0F131A;
    border: 2px solid #333A48;
    border-right: 4px solid #07080C;
    border-bottom: 4px solid #07080C;
    color: #F2F4F8;
    padding: 8px 10px;
    min-height: 20px;
    selection-background-color: #FF5D86;
    selection-color: #1A0B11;
}
QLineEdit:focus, QSpinBox:focus { border-color: #FF5D86; border-right-color: #7A1E33; border-bottom-color: #7A1E33; }
QLineEdit:disabled, QSpinBox:disabled { color: #5C6577; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; height: 0; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0; height: 0; }

/* ---------- progress ---------- */
QProgressBar {
    background: #0F131A;
    border: 2px solid #333A48;
    min-height: 12px;
    max-height: 12px;
    text-align: center;
}
QProgressBar::chunk { background: #FF5D86; }

/* ---------- scrolling ---------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #0B0D12; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #2C3340; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: #FF5D86; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

/* ---------- lists ---------- */
QListWidget { background: #0F131A; border: 2px solid #333A48; color: #F2F4F8; outline: none; }
QListWidget::item { padding: 7px 9px; border-left: 3px solid transparent; border-bottom: 1px solid #1B212B; }
QListWidget::item:hover { background: #1B2029; }
QListWidget::item:selected { background: #241820; border-left: 3px solid #FF5D86; color: #F7F9FC; }

/* ---------- checkboxes ---------- */
QCheckBox { color: #F2F4F8; spacing: 10px; padding: 3px 0; }
QCheckBox:disabled { color: #5C6577; }
QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid #3A4354; background: #0F131A; }
QCheckBox::indicator:hover { border-color: #FF86A5; }
QCheckBox::indicator:checked { background: #FF5D86; border-color: #FF9DB8; image: url(:/berry/check.png); }

/* ---------- misc ---------- */
QToolTip { background: #141821; color: #F2F4F8; border: 2px solid #FF5D86; padding: 5px 7px; }
QMenu { background: #141821; border: 2px solid #3A4354; }
QMenu::item:selected { background: #FF5D86; color: #1A0B11; }
)"));
}

}  // namespace berry::theme
