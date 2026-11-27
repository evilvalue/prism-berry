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

void apply(QWidget* root)
{
    init();
    root->setFont(regular(12));
    root->setStyleSheet(QStringLiteral(R"(
/* ---------- base ---------- */
QWidget#berryRoot { background: #0B0D12; color: #E9EDF5; }
QStackedWidget#berryPages { background: #0B0D12; }
QWidget#berryPage { background: #0B0D12; }
QWidget { color: #E9EDF5; }
QLabel { background: transparent; color: #E9EDF5; }
QLabel:disabled { color: #5C6577; }

QLabel#berryDisplay { font-size: 38px; color: #F7F9FC; }
QLabel#berryTitle { font-size: 26px; color: #F7F9FC; }
QLabel#berryH1 { font-size: 17px; color: #F7F9FC; }
QLabel#berryH2 { font-size: 14px; color: #F7F9FC; }
QLabel#berryCaption { color: #7C8798; font-size: 10px; }
QLabel#berryMuted { color: #A6B0C2; font-size: 12px; }
QLabel#berryDim { color: #7C8798; font-size: 11px; }
QLabel#berryMono { color: #A6B0C2; font-size: 11px; }
QLabel#berryAccent { color: #FF5D86; }
QLabel#berryOk { color: #4BE39F; }
QLabel#berryWarn { color: #F5B942; }
QLabel#berryErr { color: #FF6E7B; }

/* ---------- surfaces ---------- */
QFrame#berryCard { background: #141821; border: 2px solid #262C38; }
QFrame#berryHero { background: #141821; border: 2px solid #FF5D86; }
QFrame#berryShowcase { background: #0F131B; border: 2px solid #2C2333; }
QFrame#berryInset { background: #0E1219; border: 2px solid #20252F; }
QFrame#berryRule { background: #262C38; border: none; min-height: 2px; max-height: 2px; }
QFrame#berryGap { background: transparent; border: none; }

/* ---------- buttons ---------- */
QPushButton {
    background: #1B2029;
    border: 2px solid #333A48;
    color: #E9EDF5;
    padding: 8px 16px;
    min-height: 20px;
}
QPushButton:hover { background: #232A36; border-color: #FF5D86; }
QPushButton:pressed { background: #12161E; border-color: #C43A5E; }
QPushButton:disabled { color: #5C6577; border-color: #222833; background: #10141C; }

QPushButton#berryPrimary { background: #FF5D86; border-color: #C43A5E; color: #180A0F; }
QPushButton#berryPrimary:hover { background: #FF7C9E; border-color: #FF9DB8; }
QPushButton#berryPrimary:pressed { background: #E24A73; }
QPushButton#berryPrimary:disabled { background: #37222C; border-color: #2A1922; color: #7C5A66; }

QPushButton#berryPlay {
    background: #FF5D86;
    border: 2px solid #C43A5E;
    color: #180A0F;
    font-size: 20px;
    padding: 16px 30px;
}
QPushButton#berryPlay:hover { background: #FF7C9E; }
QPushButton#berryPlay:pressed { background: #E24A73; }
QPushButton#berryPlay:disabled { background: #37222C; border-color: #2A1922; color: #7C5A66; }

QPushButton#berryGhost { background: transparent; border-color: transparent; color: #98A3B5; }
QPushButton#berryGhost:hover { color: #FF5D86; border-color: transparent; background: transparent; }

QPushButton#berrySegment { background: #12161E; border-color: #2C3340; color: #98A3B5; padding: 6px 12px; }
QPushButton#berrySegment:hover { color: #E9EDF5; border-color: #FF5D86; }
QPushButton#berrySegment:checked { background: #FF5D86; border-color: #C43A5E; color: #180A0F; }

QPushButton#berryNav {
    background: transparent;
    border: none;
    border-left: 4px solid transparent;
    color: #98A3B5;
    text-align: left;
    padding: 11px 10px;
}
QPushButton#berryNav:hover { background: #12161E; color: #E9EDF5; }
QPushButton#berryNav:checked { background: #161B24; border-left: 4px solid #FF5D86; color: #FF5D86; }

/* ---------- inputs ---------- */
QLineEdit, QSpinBox {
    background: #0F131A;
    border: 2px solid #333A48;
    color: #E9EDF5;
    padding: 8px 10px;
    min-height: 20px;
    selection-background-color: #FF5D86;
    selection-color: #180A0F;
}
QLineEdit:focus, QSpinBox:focus { border-color: #FF5D86; }
QLineEdit:disabled, QSpinBox:disabled { color: #5C6577; }
QSpinBox::up-button, QSpinBox::down-button { width: 18px; background: #1B2029; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0; height: 0; }

/* ---------- progress ---------- */
QProgressBar {
    background: #0F131A;
    border: 2px solid #333A48;
    min-height: 14px;
    max-height: 14px;
    text-align: center;
}
QProgressBar::chunk { background: #FF5D86; }

/* ---------- scrolling ---------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #0B0D12; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #2C3340; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #FF5D86; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

/* ---------- lists ---------- */
QListWidget { background: #0F131A; border: 2px solid #333A48; color: #E9EDF5; outline: none; }
QListWidget::item { padding: 7px 9px; border-bottom: 1px solid #1B212B; }
QListWidget::item:hover { background: #1B2029; }
QListWidget::item:selected { background: #FF5D86; color: #180A0F; }

/* ---------- checkboxes ---------- */
QCheckBox { color: #E9EDF5; spacing: 10px; padding: 3px 0; }
QCheckBox:disabled { color: #5C6577; }
QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid #333A48; background: #0F131A; }
QCheckBox::indicator:hover { border-color: #FF7C9E; }
QCheckBox::indicator:checked { background: #FF5D86; border-color: #FF5D86; image: url(:/berry/check.png); }

/* ---------- misc ---------- */
QToolTip { background: #141821; color: #E9EDF5; border: 2px solid #FF5D86; padding: 4px 6px; }
QMenu { background: #141821; border: 2px solid #333A48; }
QMenu::item:selected { background: #FF5D86; color: #180A0F; }
)"));
}

}  // namespace berry::theme
