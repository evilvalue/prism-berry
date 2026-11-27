// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerrySkin.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include "berry/BerryConfig.h"

#include <algorithm>

namespace berry {

namespace {

constexpr qint64 MaxBytes = 32 * 1024 * 1024;
constexpr int MinSize = 64;
constexpr int MaxSize = 2048;
constexpr auto NoneCape = "known:__NONE__";

bool isImage(const QString& path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".gif"));
}

bool powerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

QString sanitize(const QString& name, bool cape)
{
    QString result = QFileInfo(name).fileName().toLower();
    result.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
    if (!result.endsWith(QLatin1String(".png")) && !result.endsWith(QLatin1String(".gif")))
        result += QStringLiteral(".png");
    if (result.size() < 5)
        result = cape ? QStringLiteral("cape.png") : QStringLiteral("skin.png");
    return result;
}

QString hex(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toHex());
}

/// Java's UUID.nameUUIDFromBytes (MD5, version 3) for OfflinePlayer:<nick>.
QString offlineUuid(const QString& nick)
{
    QByteArray digest = QCryptographicHash::hash(("OfflinePlayer:" + nick).toUtf8(), QCryptographicHash::Md5);
    digest[6] = static_cast<char>((digest[6] & 0x0f) | 0x30);
    digest[8] = static_cast<char>((digest[8] & 0x3f) | 0x80);
    const QString h = hex(digest);
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(h.mid(0, 8), h.mid(8, 4), h.mid(12, 4), h.mid(16, 4), h.mid(20, 12));
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJson(const QString& path, const QJsonObject& json)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return file.commit();
}

QJsonObject preferencesRoot()
{
    return readJson(Skins::preferencesFile());
}

void savePreferences(QJsonObject root)
{
    writeJson(Skins::preferencesFile(), root);
}

QString findHashInDirs(const QString& wanted, bool cape, QString* found)
{
    QStringList dirs{ cape ? Skins::libraryCapes() : Skins::librarySkins(),
                      cape ? Skins::instanceCapes() : Skins::instanceSkins() };
    for (const QString& dir : dirs) {
        QDirIterator it(dir, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!isImage(path))
                continue;
            QCryptographicHash sha(QCryptographicHash::Sha256);
            if (cape)
                sha.addData(QByteArrayLiteral("quickskin:cape") + QByteArray(1, '\0'));
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            sha.addData(file.readAll());
            if (hex(sha.result()) == wanted) {
                *found = path;
                return path;
            }
        }
    }
    return {};
}

QString newestInDirs(bool cape)
{
    QStringList dirs{ cape ? Skins::libraryCapes() : Skins::librarySkins(),
                      cape ? Skins::instanceCapes() : Skins::instanceSkins() };
    QString best;
    QDateTime bestTime;
    for (const QString& dir : dirs) {
        QDirIterator it(dir, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!isImage(path))
                continue;
            const QFileInfo info(path);
            if (!bestTime.isValid() || info.lastModified() > bestTime) {
                best = path;
                bestTime = info.lastModified();
            }
        }
    }
    return best;
}

}  // namespace

QString Skins::libraryDir()
{
#ifdef Q_OS_WIN
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!roaming.isEmpty())
        return QDir::cleanPath(roaming + QStringLiteral("/QuickSkin"));
#endif
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                           QStringLiteral("/QuickSkin"));
}

QString Skins::librarySkins()
{
    return libraryDir() + QStringLiteral("/skins");
}

QString Skins::libraryCapes()
{
    return libraryDir() + QStringLiteral("/capes");
}

QString Skins::instanceSkins()
{
    return QDir::cleanPath(Config::gameRoot() + QStringLiteral("/quickskin/uploads/skins"));
}

QString Skins::instanceCapes()
{
    return QDir::cleanPath(Config::gameRoot() + QStringLiteral("/quickskin/uploads/capes"));
}

QString Skins::clientConfig()
{
    return QDir::cleanPath(Config::gameRoot() + QStringLiteral("/config/quickskin-client.json"));
}

QString Skins::preferencesFile()
{
    return libraryDir() + QStringLiteral("/skin-preferences.json");
}

QString Skins::availability()
{
    const QString mods = Config::gameRoot() + QStringLiteral("/mods");
    if (!QDir(mods).exists())
        return QStringLiteral("Папка модов ещё не создана — обнови сборку.");
    QDirIterator it(mods, QDir::Files);
    while (it.hasNext()) {
        const QString name = QFileInfo(it.next()).fileName().toLower();
        if (name.endsWith(QLatin1String(".jar")) && name.contains(QLatin1String("quick")) &&
            name.contains(QLatin1String("skin")))
            return {};
    }
    return QStringLiteral("Мод Quick Skin не установлен — включи его в настройках.");
}

QString Skins::hashOf(const QString& path, bool cape)
{
    QCryptographicHash sha(QCryptographicHash::Sha256);
    if (cape)
        sha.addData(QByteArrayLiteral("quickskin:cape") + QByteArray(1, '\0'));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    while (!file.atEnd())
        sha.addData(file.read(65536));
    return hex(sha.result());
}

QString Skins::copyInto(const QString& source, bool cape, QString* error)
{
    const QFileInfo info(source);
    if (!info.isFile()) {
        *error = QStringLiteral("файл не найден");
        return {};
    }
    if (info.size() > MaxBytes) {
        *error = QStringLiteral("файл больше 32 МБ — Quick Skin его не примет");
        return {};
    }
    if (!isImage(source)) {
        *error = QStringLiteral("Quick Skin принимает только PNG и GIF");
        return {};
    }

    QImageReader reader(source);
    const QSize size = reader.size();
    if (!size.isValid()) {
        *error = QStringLiteral("не удалось прочитать картинку");
        return {};
    }
    if (cape) {
        if (!powerOfTwo(size.width()) || size.width() < MinSize || size.width() > MaxSize ||
            size.height() % (size.width() / 2) != 0) {
            *error = QStringLiteral("плащ должен быть 2:1: 64×32, 128×64, 256×128 … 2048×1024");
            return {};
        }
    } else if (!(size.width() == 64 && size.height() == 32) &&
               !(size.width() == size.height() && powerOfTwo(size.width()) && size.width() >= MinSize &&
                 size.width() <= MaxSize)) {
        *error = QStringLiteral("скин должен быть 64×32 или квадратом 64×64 … 2048×2048");
        return {};
    }

    // QuickSkin 5+ reads the shared library; older versions read the instance pool.
    QDir library(libraryDir());
    const QString dir = library.exists() ? (cape ? libraryCapes() : librarySkins())
                                         : (cape ? instanceCapes() : instanceSkins());
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("не удалось создать папку Quick Skin: %1").arg(dir);
        return {};
    }

    QString target = dir + QLatin1Char('/') + sanitize(info.fileName(), cape);
    if (QFileInfo::exists(target) && hashOf(target, cape) != hashOf(source, cape)) {
        const QString base = target.left(target.size() - 4);
        const QString ext = target.right(4);
        for (int i = 2; i < 1000; ++i) {
            const QString candidate = QStringLiteral("%1-%2%3").arg(base).arg(i).arg(ext);
            if (!QFileInfo::exists(candidate)) {
                target = candidate;
                break;
            }
        }
    }
    QFile::remove(target);
    if (!QFile::copy(source, target)) {
        *error = QStringLiteral("не удалось скопировать файл в %1").arg(dir);
        return {};
    }
    return target;
}

void Skins::editConfig(const std::function<void(QJsonObject&)>& edit)
{
    const QString path = clientConfig();
    QJsonObject json = readJson(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo::exists(path))
        QFile::copy(path, path + QStringLiteral(".bak"));
    edit(json);
    writeJson(path, json);
}

void Skins::writeSkinConfig(const QString& hash, const QString& nick, const QString& model)
{
    const QString normalized = model == QLatin1String("classic") || model == QLatin1String("slim")
                                   ? model
                                   : QStringLiteral("auto");
    editConfig([&](QJsonObject& json) {
        json.insert(QStringLiteral("activeSkinHash"), QStringLiteral("sha256-") + hash);
        json.insert(QStringLiteral("playerOwnSkinHash"), QStringLiteral("sha256-") + hash);
        json.insert(QStringLiteral("playerOwnSkinHidden"), false);
        json.insert(QStringLiteral("activeModelType"), normalized);
        if (!nick.isEmpty()) {
            const QString account = offlineUuid(nick) + QLatin1Char(':') + nick;
            json.insert(QStringLiteral("playerOwnSkinAccount"), account);
            QJsonObject accounts = json.value(QStringLiteral("accountSkins")).toObject();
            QJsonObject skins = accounts.value(QStringLiteral("skins")).toObject();
            skins.insert(account, QStringLiteral("sha256-") + hash);
            accounts.insert(QStringLiteral("skins"), skins);
            accounts.insert(QStringLiteral("migrated"), true);
            json.insert(QStringLiteral("accountSkins"), accounts);
        }
    });
}

void Skins::writeCapeConfig(const QString& hash)
{
    editConfig([&](QJsonObject& json) { json.insert(QStringLiteral("activeCapeHash"), hash); });
}

void Skins::writePreference(const QString& hash, const QString& model, const QString& cape)
{
    if (hash.isEmpty())
        return;
    QJsonObject root = preferencesRoot();
    QJsonObject prefs = root.value(QStringLiteral("preferences")).toObject();
    QJsonObject entry = prefs.value(hash).toObject();
    if (!model.isEmpty())
        entry.insert(QStringLiteral("modelType"), model == QLatin1String("slim") ? QStringLiteral("slim")
                                                                                : QStringLiteral("classic"));
    if (!entry.contains(QStringLiteral("favorite")))
        entry.insert(QStringLiteral("favorite"), false);
    if (!cape.isEmpty())
        entry.insert(QStringLiteral("cape"), cape);
    prefs.insert(hash, entry);
    root.insert(QStringLiteral("preferences"), prefs);
    savePreferences(root);
}

bool Skins::applySkin(const QString& source, const QString& nick, const QString& model, QString* error)
{
    const QString target = copyInto(source, false, error);
    if (target.isEmpty())
        return false;
    const QString hash = hashOf(target, false);
    writeSkinConfig(hash, nick, model);
    writePreference(QStringLiteral("sha256-") + hash, model, QString());
    return true;
}

bool Skins::activateSkin(const QString& path, const QString& nick, const QString& model, QString* error)
{
    if (!QFileInfo::exists(path)) {
        *error = QStringLiteral("файл скина не найден");
        return false;
    }
    const QString hash = hashOf(path, false);
    if (hash.isEmpty()) {
        *error = QStringLiteral("не удалось прочитать скин");
        return false;
    }
    writeSkinConfig(hash, nick, model);
    writePreference(QStringLiteral("sha256-") + hash, model, QString());
    return true;
}

bool Skins::applyCape(const QString& source, QString* error)
{
    const QString target = copyInto(source, true, error);
    if (target.isEmpty())
        return false;
    const QString hash = hashOf(target, true);
    writeCapeConfig(QStringLiteral("sha256-") + hash);
    writePreference(QStringLiteral("sha256-") + activeSkinHash(), QString(), QStringLiteral("sha256-") + hash);
    return true;
}

bool Skins::activateCape(const QString& path, QString* error)
{
    if (!QFileInfo::exists(path)) {
        *error = QStringLiteral("файл плаща не найден");
        return false;
    }
    const QString hash = hashOf(path, true);
    writeCapeConfig(QStringLiteral("sha256-") + hash);
    writePreference(QStringLiteral("sha256-") + activeSkinHash(), QString(), QStringLiteral("sha256-") + hash);
    return true;
}

void Skins::clearCape()
{
    writeCapeConfig(QString::fromLatin1(NoneCape));
}

QString Skins::model()
{
    const QString value = readJson(clientConfig()).value(QStringLiteral("activeModelType")).toString();
    return value == QLatin1String("classic") || value == QLatin1String("slim") ? value : QStringLiteral("auto");
}

void Skins::setModel(const QString& model)
{
    const QString normalized = model == QLatin1String("classic") || model == QLatin1String("slim")
                                   ? model
                                   : QStringLiteral("auto");
    editConfig([&](QJsonObject& json) { json.insert(QStringLiteral("activeModelType"), normalized); });
    writePreference(QStringLiteral("sha256-") + activeSkinHash(), normalized, QString());
}

QString Skins::activeSkinHash()
{
    const QString value = readJson(clientConfig()).value(QStringLiteral("activeSkinHash")).toString();
    return value.startsWith(QLatin1String("sha256-")) ? value.mid(7) : QString();
}

QString Skins::activeCapeHash()
{
    const QString value = readJson(clientConfig()).value(QStringLiteral("activeCapeHash")).toString();
    return value;
}

QString Skins::currentSkin()
{
    QString found;
    const QString hash = activeSkinHash();
    if (!hash.isEmpty() && !findHashInDirs(hash, false, &found).isEmpty())
        return found;
    return newestInDirs(false);
}

QString Skins::currentCape()
{
    QString found;
    const QString hash = activeCapeHash();
    if (hash.startsWith(QLatin1String("sha256-")) && !findHashInDirs(hash.mid(7), true, &found).isEmpty())
        return found;
    return {};
}

QVector<Skins::Entry> Skins::entries(bool cape)
{
    QVector<Entry> list;
    const QString active = cape ? activeCapeHash() : QStringLiteral("sha256-") + activeSkinHash();
    for (const QString& dir : { cape ? libraryCapes() : librarySkins(),
                                cape ? instanceCapes() : instanceSkins() }) {
        QDirIterator it(dir, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!isImage(path))
                continue;
            Entry entry;
            entry.path = path;
            entry.name = QFileInfo(path).fileName();
            entry.hash = QStringLiteral("sha256-") + hashOf(path, cape);
            entry.active = entry.hash == active;
            const QSize size = QImageReader(path).size();
            entry.size = size.isValid() ? QStringLiteral("%1×%2").arg(size.width()).arg(size.height()) : QString();
            list.append(entry);
        }
    }
    std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return list;
}

bool Skins::removeEntry(const QString& path, QString* error)
{
    if (!QFile::remove(path)) {
        *error = QStringLiteral("не удалось удалить файл");
        return false;
    }
    return true;
}

QImage Skins::preview(const QImage& skin, bool slim, int scale)
{
    if (skin.isNull())
        return {};

    const int width = 64 * 2 + 40;
    const int height = 32 * 2 + 20;
    QImage canvas(width, height, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const qreal texel = skin.width() / 64.0;
    const auto part = [&](int u, int v, int w, int h, int x, int y) {
        painter.drawImage(QRectF(x * 2, y * 2, w * 2, h * 2), skin,
                          QRectF(u * texel, v * texel, w * texel, h * texel));
    };

    const int armWidth = slim ? 3 : 4;
    const int centerX = (width - 16) / 2;
    const int topY = 2;

    // legs, body, arms, head — front view
    part(4, 20, 4, 12, centerX + 5, topY + 20);
    part(20, 52, 4, 12, centerX + 7, topY + 20);
    part(0, 36, 4, 12, centerX + 5, topY + 20);
    part(4, 36, 4, 12, centerX + 7, topY + 20);
    part(20, 20, 8, 12, centerX + 4, topY + 8);
    part(20, 36, 8, 12, centerX + 4, topY + 8);
    part(44, 20, armWidth, 12, centerX + 4 - armWidth, topY + 8);
    part(44, 36, armWidth, 12, centerX + 4 - armWidth, topY + 8);
    part(36, 52, armWidth, 12, centerX + 12, topY + 8);
    part(52, 52, armWidth, 12, centerX + 12, topY + 8);
    part(8, 8, 8, 8, centerX + 4, topY);
    part(40, 8, 8, 8, centerX + 4, topY);

    painter.end();
    return canvas.scaled(canvas.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
}

bool Skins::detectSlim(const QImage& skin)
{
    if (skin.isNull() || skin.width() < 64)
        return false;
    // Slim skins have transparent pixels where the classic arm would be.
    return skin.pixelColor(46, 52).alpha() == 0 && skin.pixelColor(54, 20).alpha() == 0;
}

}  // namespace berry
