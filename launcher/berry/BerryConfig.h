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
#include <QUrl>

namespace berry {

/// Everything about the one server this build exists for.
class Config {
   public:
    /// Folder name of the instance in the launcher's instances directory.
    static QString instanceId();
    /// Human readable name shown by the UI.
    static QString instanceName();

    static QString serverHost();
    static quint16 serverPort();
    /// "host:port", the form Minecraft's --server/--quickPlayMultiplayer expects.
    static QString serverAddress();

    /// berryauth server: the Telegram bot issues the login token here.
    static QString authApi();

    /// packwiz index (HTTPS manifest with a sha256 for every file).
    static QUrl packIndexUrl();
    /// Absolute URL of a file listed in the index.
    static QUrl packFileUrl(const QString& relativePath);

    static QString instancesRoot();
    static QString instanceRoot();
    /// Game directory of the instance ("minecraft" next to instance.cfg).
    static QString gameRoot();
};

}  // namespace berry
