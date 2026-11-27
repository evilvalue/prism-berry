// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QObject>
#include <QString>

namespace berry {

/// Minecraft "status" ping: MOTD, online players and the TCP round trip.
/// Runs on a worker thread, so the window never freezes.
class ServerPing : public QObject {
    Q_OBJECT

   public:
    struct Status {
        bool ok = false;
        QString error;
        QString motd;
        QString version;
        int online = -1;
        int max = -1;
        int ping = -1;
    };

    explicit ServerPing(QObject* parent = nullptr);

    void query(const QString& host, quint16 port, int timeoutMs = 4000);
    bool isRunning() const { return m_running; }

   signals:
    void finished(const berry::ServerPing::Status& status);

   private:
    bool m_running = false;
};

}  // namespace berry
