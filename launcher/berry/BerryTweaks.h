// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace berry {

/// BerryUp's memory savers, ported: optional client mods are moved between
/// mods/ and berry-off/ instead of being deleted, so NeoForge does not load them.
class Tweaks {
   public:
    struct Mod {
        QString key;
        QString label;
        QString hint;
        bool defaultOn = true;
        QStringList prefixes;
        QString url;
        QString urlFallback;
        QString sha512;
        QString filename;
        qint64 size = 0;

        bool downloadable() const { return !url.isEmpty() && !filename.isEmpty(); }
    };

    static const QVector<Mod>& mods();
    static bool modEnabled(const Mod& mod);
    static void setModEnabled(const Mod& mod, bool enabled);

    static bool packsEnabled();
    static void setPacksEnabled(bool enabled);
    static bool shadersEnabled();
    static void setShadersEnabled(bool enabled);

    /// Moves jars, rewrites options.txt / iris.properties and fixes Distant Horizons.
    static void applyAll();

    /// Downloads a mod that is not part of the server pack (Distant Horizons).
    static void installOptionalMod(const Mod& mod, std::function<void(bool ok, const QString& error)> done);

    static int modCount();
    static int offCount();

    /// Moves the player's configs aside so the pack can restore its defaults.
    static QString resetConfigs();

    /// Short launch summary used by the diagnostics page.
    static QString summary();
};

}  // namespace berry
