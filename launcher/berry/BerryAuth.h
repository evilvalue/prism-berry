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

class QNetworkReply;

namespace berry {

/// Login through the Telegram bot, with no Microsoft account involved.
///
/// The launcher posts only the nick to the berryauth server, the server returns
/// a one-time t.me link, the bot whitelists the nick on Start, and the launcher
/// polls for the signed token. The token ends up in
/// <game dir>/config/berryauth-token.json where the client mod reads it.
class TelegramAuth : public QObject {
    Q_OBJECT

   public:
    enum class TokenState { None, Ok, Expired, OtherNick, Broken };

    explicit TelegramAuth(QObject* parent = nullptr);

    TokenState tokenState() const;
    QString tokenNick() const;
    /// Nick remembered by this launcher (also used for the offline Prism account).
    QString nick() const;

    bool pending() const;
    QString pendingLink() const;

    /// Validates the nick, asks the server for a link and opens Telegram.
    void start(const QString& nick);
    /// One status check; safe to call from a timer while pending().
    void poll();
    /// Re-opens an already issued link instead of creating a new one.
    bool openPendingLink();
    /// Opens a t.me link in Telegram Desktop when it is installed, otherwise
    /// in the browser. Works for both https://t.me/... and tg:// forms.
    static bool openTelegramLink(const QString& link);
    /// Removes the saved token and forgets the nick.
    void logout();

    /// Makes sure Prism has an offline account named `nick` and selects it.
    static bool useOfflineAccount(const QString& nick);

   signals:
    /// Human readable line for the account block; `error` makes it red.
    void message(const QString& text, bool error);
    /// Emitted after a successful verification; the token file is already written.
    void authenticated(const QString& nick);

   private:
    void requestLink(const QString& nick);
    void requestStatus();
    void handleJson(QNetworkReply* reply, int httpStatus, const QByteArray& body);
    bool writeToken(const QString& nick, const QString& token);
    void clearPending();
    QString tokenFilePath() const;

    QString m_pendingId;
    QString m_pendingSecret;
    QString m_pendingNick;
    QString m_pendingLink;
    qint64 m_pendingUntil = 0;
    bool m_busy = false;
};

}  // namespace berry
