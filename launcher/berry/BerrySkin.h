// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QImage>
#include <functional>

class QJsonObject;
#include <QString>
#include <QVector>

namespace berry {

/// QuickSkin 5 integration: reads and writes the same library and config the mod
/// uses, so a skin installed here is the skin shown in game.
class Skins {
   public:
    struct Entry {
        QString path;
        QString name;
        QString hash;
        QString size;
        bool active = false;
    };

    /// %APPDATA%\QuickSkin (Roaming), the shared library of QuickSkin 5+.
    static QString libraryDir();
    static QString librarySkins();
    static QString libraryCapes();
    /// Old per-instance pool used by QuickSkin 4 and earlier.
    static QString instanceSkins();
    static QString instanceCapes();
    static QString clientConfig();
    static QString preferencesFile();

    /// Empty string when the QuickSkin jar is installed, otherwise a reason.
    static QString availability();

    static bool applySkin(const QString& source, const QString& nick, const QString& model, QString* error);
    static bool activateSkin(const QString& path, const QString& nick, const QString& model, QString* error);
    static bool applyCape(const QString& source, QString* error);
    static bool activateCape(const QString& path, QString* error);
    static void clearCape();

    /// auto, classic or slim.
    static QString model();
    static void setModel(const QString& model);

    static QString activeSkinHash();
    static QString activeCapeHash();
    /// Path of the skin/cape currently selected in game (may be empty).
    static QString currentSkin();
    static QString currentCape();

    static QVector<Entry> entries(bool cape);
    static bool removeEntry(const QString& path, QString* error);

    /// Nearest-neighbour 2D character preview built from a 64x64 skin.
    static QImage preview(const QImage& skin, bool slim, int scale);
    static bool detectSlim(const QImage& skin);

   private:
    static QString hashOf(const QString& path, bool cape);
    static QString copyInto(const QString& source, bool cape, QString* error);
    static void writeSkinConfig(const QString& hash, const QString& nick, const QString& model);
    static void writeCapeConfig(const QString& hash);
    static void writePreference(const QString& hash, const QString& model, const QString& cape);
    static void editConfig(const std::function<void(QJsonObject&)>& edit);
};

}  // namespace berry
