// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class QCryptographicHash;
class QNetworkReply;
class QSaveFile;

namespace berry {

/// Keeps the instance's game folder in sync with the server's packwiz index.
///
/// The manifest (index.toml) lists every file with a sha256; packwiz metafiles
/// (".pw.toml") additionally point at a download and carry sha256/sha512 of it.
/// Files are written straight into the instance with QSaveFile, so there are no
/// temporary archives and no helper processes.
class PackSync : public QObject {
    Q_OBJECT

   public:
    explicit PackSync(QObject* parent = nullptr);

    /// Runs one update pass. Emits progress() and then finished() exactly once.
    void start();
    void abort();
    bool isRunning() const { return m_running; }

   signals:
    void progress(int completed, int total, const QString& currentFile);
    void finished(bool ok, const QString& error);

   private:
    struct Entry {
        QString hashFormat = QStringLiteral("sha256");
        QByteArray hash;
        bool metafile = false;
    };

    struct Job {
        enum class Kind { Blob, Metafile };

        Kind kind = Kind::Blob;
        /// Index entry this job finishes, used for progress accounting.
        QString entryPath;
        /// File to write, relative to the game root.
        QString diskPath;
        QUrl url;
        QString hashFormat;
        QByteArray hash;
    };

    void fetchIndex();
    void parseIndex(const QByteArray& data);
    void scanLocalFiles();
    void planMetafiles();
    void enqueue(const Job& job);
    void startNextDownloads();
    void startDownload(const Job& job);
    void onReplyFinished(QNetworkReply* reply, const Job& job, QSaveFile* file, QCryptographicHash* hasher);
    void onMetafileInstalled(const Job& job, const QString& localPath);
    void completeEntry(const QString& entryPath);
    void maybeFinish();
    void fail(const QString& message);

    static bool readMetafile(const QString& localPath, const QString& metafilePath, Job* payload, QString* error);
    static QCryptographicHash::Algorithm algorithmFor(const QString& format);
    static QByteArray hashFile(const QString& path, const QString& format);

    QHash<QString, Entry> m_entries;
    QQueue<Job> m_queue;
    QSet<QNetworkReply*> m_replies;
    QSet<QString> m_queuedPaths;
    QSet<QString> m_doneEntries;
    QStringList m_errors;
    QString m_root;
    int m_total = 0;
    int m_completed = 0;
    int m_inFlight = 0;
    bool m_running = false;
};

}  // namespace berry
