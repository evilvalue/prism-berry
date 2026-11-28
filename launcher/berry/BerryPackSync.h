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
#include <QVector>

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

    struct Job {
        enum class Kind { Blob, Metafile };

        Kind kind = Kind::Blob;
        /// Index entry this job finishes, used for progress accounting.
        QString entryPath;
        /// True when the pack's copy is written next to an edited player file
        /// ("config/x.toml" -> "config/x.toml.new") instead of over it.
        bool sidecar = false;
        /// Set for the jar a ".pw.toml" metafile points at: it is not an index
        /// entry, but the state file has to remember it anyway, or a mod the
        /// pack drops could never be found on disk again.
        bool fromMetafile = false;
        /// File to write, relative to the game root.
        QString diskPath;
        QUrl url;
        QString hashFormat;
        QByteArray hash;
        int retries = 0;
    };

    /// Off-thread result of hashing the local game folder and planning downloads.
    struct Plan {
        QStringList satisfied;
        QVector<Job> jobs;
        QStringList errors;
        /// Files the pack shipped once and no longer lists, still byte-identical
        /// to what was installed: leftovers of a removed mod, not the player's.
        QStringList removed;
    };

    struct MetafileResult {
        bool ok = false;
        bool payloadReady = false;
        Job payload;
        QString error;
    };

   signals:
    void progress(int completed, int total, const QString& currentFile);
    void finished(bool ok, const QString& error);

   private:
    struct Entry {
        QString hashFormat = QStringLiteral("sha256");
        QByteArray hash;
        bool metafile = false;
    };

    /// What the pack installed last time. The hash format travels with the
    /// hash: a file the current manifest no longer lists has no other way to
    /// be verified before it is deleted.
    struct SavedFile {
        QString hashFormat = QStringLiteral("sha256");
        QByteArray hash;
    };

    void fetchIndex();
    void parseIndex(const QByteArray& data);
    void scanLocalFiles();
    static Plan buildPlan(const QString& root, const QHash<QString, Entry>& entries,
                          const QHash<QString, SavedFile>& state);
    void enqueue(const Job& job);
    void startNextDownloads();
    void startDownload(const Job& job);
    void onReplyFinished(QNetworkReply* reply, const Job& job, QSaveFile* file, QCryptographicHash* hasher);
    void onMetafileInstalled(const Job& job, const QString& localPath);
    void retryLater(const Job& job);
    void completeEntry(const QString& entryPath);
    void maybeFinish();
    void fail(const QString& message);
    /// Pack hashes applied on the last sync, in .berry-sync.json. Knowing what
    /// the pack shipped last time is what tells an untouched config apart from
    /// one the player edited.
    void loadState();
    void saveState();

    static bool readMetafile(const QString& localPath, const QString& metafilePath, Job* payload, QString* error);
    static QCryptographicHash::Algorithm algorithmFor(const QString& format);
    static QByteArray hashFile(const QString& path, const QString& format);

    QHash<QString, Entry> m_entries;
    QHash<QString, SavedFile> m_state;
    /// Metafile payloads (jars) installed during this pass, for saveState().
    QHash<QString, SavedFile> m_installed;
    QString m_statePath;
    QQueue<Job> m_queue;
    QSet<QNetworkReply*> m_replies;
    QSet<QString> m_queuedPaths;
    QSet<QString> m_doneEntries;
    QStringList m_errors;
    QString m_root;
    int m_total = 0;
    int m_completed = 0;
    int m_inFlight = 0;
    int m_pendingMetafiles = 0;
    int m_pendingRetries = 0;
    bool m_running = false;
};

}  // namespace berry
