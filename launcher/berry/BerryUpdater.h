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
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace berry {

/// Checks GitHub Releases for a newer BerryTrip installer, downloads it into
/// the temp folder, verifies the published SHA-256 and can hand it to a tiny
/// cmd helper that waits for the launcher to exit, installs silently and
/// starts the new build again.
class Updater : public QObject {
    Q_OBJECT

   public:
    explicit Updater(QObject* parent = nullptr);

    /// One release check at a time. Emits checking() and then exactly one of
    /// upToDate(), available() or checkFailed().
    void check();

    /// Downloads the installer found by check(). Emits downloading(), then
    /// downloadProgress() and finally readyToInstall() or failed().
    void download();

    /// Starts the detached install helper. The caller should quit the app when
    /// this returns true.
    bool install(QString* error);

    bool isDownloading() const { return m_downloading; }
    bool hasInstaller() const { return m_installerReady; }
    QString version() const { return m_version; }
    QString installerPath() const { return m_installerPath; }

    static bool isNewer(const QString& candidate, const QString& current);

   signals:
    void checking();
    void upToDate(const QString& currentVersion);
    void available(const QString& version, const QString& notes, const QUrl& releasePage, const QUrl& installerUrl,
                   const QString& digest, qint64 size);
    void checkFailed(const QString& error);
    void downloading(const QString& version);
    void downloadProgress(qint64 received, qint64 total);
    void readyToInstall(const QString& installerPath);
    void failed(const QString& error);

   private:
    void handleCheckReply(QNetworkReply* reply);
    void handleDownloadReply(QNetworkReply* reply);
    void failDownload(const QString& error);
    static QString normalizedVersion(QString version);

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_checkReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
    QFile* m_file = nullptr;

    QString m_version;
    QString m_notes;
    QString m_digest;
    QString m_installerPath;
    QUrl m_releasePage;
    QUrl m_installerUrl;
    qint64 m_expectedSize = -1;
    bool m_downloading = false;
    bool m_installerReady = false;
};

}  // namespace berry
