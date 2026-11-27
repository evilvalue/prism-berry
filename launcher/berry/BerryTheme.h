// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QWidget;

namespace berry {

/// BerryUp's pixel palette and stylesheet: hard corners, two pixel borders,
/// flat colours and the Monocraft font.
namespace theme {

inline const QColor Bg{ 0x0E, 0x10, 0x15 };
inline const QColor BgSoft{ 0x12, 0x15, 0x1B };
inline const QColor Surface{ 0x17, 0x1B, 0x23 };
inline const QColor SurfaceAlt{ 0x20, 0x25, 0x2F };
inline const QColor Hover{ 0x2A, 0x30, 0x3C };
inline const QColor Line{ 0x39, 0x40, 0x4F };
inline const QColor LineSoft{ 0x28, 0x2E, 0x3A };
inline const QColor Shadow{ 0x07, 0x08, 0x0C };

inline const QColor Fg{ 0xF2, 0xF4, 0xF8 };
inline const QColor Muted{ 0xA9, 0xB2, 0xC3 };
inline const QColor Dim{ 0x74, 0x7E, 0x90 };

inline const QColor Accent{ 0xFF, 0x5D, 0x86 };
inline const QColor AccentHover{ 0xFF, 0x7C, 0x9E };
inline const QColor AccentDeep{ 0xB6, 0x2E, 0x52 };
inline const QColor Accent2{ 0xFF, 0xA3, 0x6B };
inline const QColor Violet{ 0x9B, 0x87, 0xFF };
inline const QColor Ok{ 0x4B, 0xE3, 0x9F };
inline const QColor Warn{ 0xF5, 0xB9, 0x42 };
inline const QColor Err{ 0xFF, 0x6E, 0x7B };
inline const QColor Telegram{ 0x38, 0xAE, 0xE0 };
inline const QColor OnAccent{ 0x1A, 0x0B, 0x11 };

/// Registers the bundled Monocraft fonts; safe to call more than once.
void init();

QString family();
QFont regular(int pixelSize);
QFont semiBold(int pixelSize);

/// Applies the pixel look to the panel and everything inside it.
void apply(QWidget* root);

}  // namespace theme
}  // namespace berry
