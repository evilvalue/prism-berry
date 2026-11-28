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
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QPainter;
class QWidget;

namespace berry {

/// Pixel-art design system.
///
/// Rules the whole launcher follows: an integer pixel grid, no antialiasing,
/// no rounded corners and no blurred shadows; 2px hard borders with a 1px
/// light edge on the top/left and a 1px dark edge on the bottom/right; a
/// limited palette of flat colours (a three-step ramp per hue); 4px spacing.
namespace theme {

// ---------------------------------------------------------------- palette
inline const QColor Ink{ 0x05, 0x07, 0x0A };         // outer pixel outline
inline const QColor Bg{ 0x0A, 0x0D, 0x12 };          // window background
inline const QColor BgDeep{ 0x07, 0x09, 0x0D };      // holes, inputs
inline const QColor BgDot{ 0x0C, 0x10, 0x16 };       // background dither
inline const QColor Surface{ 0x15, 0x1A, 0x22 };     // cards
inline const QColor SurfaceLo{ 0x10, 0x14, 0x1B };   // sunken strips
inline const QColor SurfaceHi{ 0x1D, 0x24, 0x30 };   // hover / raised
inline const QColor Line{ 0x36, 0x40, 0x52 };        // normal border
inline const QColor LineSoft{ 0x22, 0x2A, 0x37 };    // inner separators

inline const QColor Fg{ 0xF2, 0xF5, 0xFA };
inline const QColor Muted{ 0x9E, 0xA9, 0xBC };
inline const QColor Dim{ 0x70, 0x7C, 0x90 };

inline const QColor Accent{ 0xFF, 0x4F, 0x79 };      // berry pink
inline const QColor AccentHi{ 0xFF, 0x9D, 0xB4 };    // top/left highlight
inline const QColor AccentDeep{ 0xB3, 0x25, 0x46 };  // border / pressed
inline const QColor AccentDark{ 0x6E, 0x14, 0x30 };  // bottom/right shadow

inline const QColor Ok{ 0x52, 0xE3, 0x9C };
inline const QColor OkDeep{ 0x1E, 0x6B, 0x4B };
inline const QColor Warn{ 0xFF, 0xC2, 0x4B };
inline const QColor WarnDeep{ 0x6E, 0x53, 0x20 };
inline const QColor Err{ 0xFF, 0x6B, 0x6B };
inline const QColor ErrDeep{ 0x6E, 0x24, 0x24 };
inline const QColor Violet{ 0xA9, 0x8B, 0xFF };
inline const QColor Telegram{ 0x45, 0xB6, 0xE8 };
inline const QColor OnAccent{ 0x14, 0x07, 0x0C };

/// Registers the bundled Minecraft Rus pixel font; safe to call more than once.
void init();

QString family();
QFont regular(int pixelSize);
QFont semiBold(int pixelSize);
QFont spaced(int pixelSize, qreal spacing, bool bold = false);

/// Draws a `#`-glyph on an integer pixel grid with a 1px dark outline.
QPixmap pixelPixmap(const QStringList& glyph, const QColor& color, int scale = 1);
/// Draws a glyph whose characters map to a palette (see PixelPalette).
QPixmap pixelArt(const QStringList& rows, const QColor& body, int scale = 1);
/// The same glyph as a crisp icon with 1x/2x/3x variants for scaled displays.
QIcon pixelIcon(const QStringList& glyph, const QColor& color);

/// Channel/ramp helper: mixes two colours on whole RGB steps.
QColor mix(const QColor& a, const QColor& b, int percent);

/// Draws a pixel bevel: 2px border, light top/left edge, dark bottom/right.
void bevel(QPainter& painter, const QRect& rect, const QColor& fill, const QColor& border, const QColor& highlight,
           const QColor& shadow);
/// Draws a sunken 1px-inset hole used by inputs, progress bars and lists.
void inset(QPainter& painter, const QRect& rect, const QColor& fill, const QColor& border);

/// Applies the pixel look to the panel and everything inside it.
void apply(QWidget* root);

/// Applies the same pixel look to the whole application (dialogs, menus,
/// Prism leftovers). Call once from the application entry point.
void applyApplication();

/// True reference to the bundled old BerryTrip icon.
QIcon appIcon();

/// Elides text to fit pixelSize with Minecraft Rus (wide metrics).
QString elide(const QString& text, int pixelSize, qreal spacing, int maxWidth);

}  // namespace theme
}  // namespace berry
