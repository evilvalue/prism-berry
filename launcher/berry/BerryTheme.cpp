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

    g_family = registerFont(QStringLiteral(":/berry/Monocraft.ttf"));
    g_familySemi = registerFont(QStringLiteral(":/berry/Monocraft-SemiBold.ttf"));
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
    root->setFont(regular(13));
    root->setStyleSheet(QStringLiteral(R"(
QWidget#berryRoot { background: #0E1015; color: #F2F4F8; }
QLabel { color: #F2F4F8; background: transparent; }
QLabel#berryTitle { font-size: 30px; color: #F2F4F8; }
QLabel#berryH1 { font-size: 20px; color: #F2F4F8; }
QLabel#berryCaption { color: #747E90; font-size: 11px; }
QLabel#berryMuted { color: #A9B2C3; }
QLabel#berryDim { color: #747E90; }
QLabel#berryAccent { color: #FF5D86; }
QLabel#berryOk { color: #4BE39F; }
QLabel#berryWarn { color: #F5B942; }
QLabel#berryErr { color: #FF6E7B; }

QFrame#berryCard { background: #171B23; border: 2px solid #39404F; }
QFrame#berryHero { background: #171B23; border: 2px solid #FF5D86; }
QFrame#berryRule { background: #282E3A; border: none; max-height: 2px; min-height: 2px; }

QPushButton { background: #20252F; border: 2px solid #39404F; padding: 7px 14px; color: #F2F4F8; }
QPushButton:hover { background: #2A303C; border-color: #FF5D86; }
QPushButton:pressed { background: #171B23; border-color: #B62E52; }
QPushButton:disabled { color: #747E90; border-color: #282E3A; background: #12151B; }
QPushButton#berryPrimary { background: #FF5D86; border: 2px solid #B62E52; color: #1A0B11; font-weight: bold; }
QPushButton#berryPrimary:hover { background: #FF7C9E; }
QPushButton#berryPrimary:disabled { background: #3A2430; border-color: #2A1A22; color: #8B6A76; }
QPushButton#berryGhost { background: transparent; border: 2px solid transparent; color: #A9B2C3; }
QPushButton#berryGhost:hover { color: #FF5D86; border-color: #39404F; }
QPushButton#berryNav { background: transparent; border: 2px solid transparent; color: #A9B2C3; text-align: left; padding: 9px 12px; }
QPushButton#berryNav:hover { color: #F2F4F8; border-color: #282E3A; }
QPushButton#berryNav:checked { background: #20252F; border-color: #FF5D86; color: #FF5D86; }
QPushButton#berryPlay { font-size: 18px; padding: 14px 30px; }

QLineEdit, QSpinBox { background: #12151B; border: 2px solid #39404F; padding: 6px 8px; color: #F2F4F8; selection-background-color: #FF5D86; selection-color: #1A0B11; }
QLineEdit:focus, QSpinBox:focus { border-color: #FF5D86; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; background: #20252F; border: none; }

QProgressBar { background: #12151B; border: 2px solid #39404F; max-height: 10px; min-height: 10px; }
QProgressBar::chunk { background: #4BE39F; }

QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: #12151B; width: 10px; border: none; }
QScrollBar::handle:vertical { background: #39404F; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #FF5D86; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }

QCheckBox { color: #F2F4F8; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 2px solid #39404F; background: #12151B; }
QCheckBox::indicator:hover { border-color: #FF5D86; }
QCheckBox::indicator:checked { background: #4BE39F; border-color: #4BE39F; }

QToolTip { background: #171B23; color: #F2F4F8; border: 2px solid #39404F; padding: 4px; }
)"));
}

}  // namespace berry::theme
