// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryInstance.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "Application.h"
#include "InstanceList.h"
#include "berry/BerryConfig.h"
#include "minecraft/MinecraftInstance.h"

namespace berry {

namespace {

constexpr auto MinecraftVersion = "1.21.1";
constexpr auto NeoForgeVersion = "21.1.267";
constexpr auto LwjglVersion = "3.3.3";

bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Не удалось записать %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QByteArray instanceConfig()
{
    return QByteArrayLiteral(
        "[General]\n"
        "ConfigVersion=1.3\n"
        "InstanceType=OneSix\n"
        "name=BerryTrip\n"
        "OverrideMemory=true\n"
        "MinMemAlloc=2048\n"
        "MaxMemAlloc=8192\n"
        "AutomaticJava=true\n");
}

QByteArray componentManifest()
{
    const QJsonObject lwjgl{
        { "cachedName", "LWJGL 3" },   { "cachedVersion", LwjglVersion }, { "cachedVolatile", true },
        { "dependencyOnly", true },    { "uid", "org.lwjgl3" },            { "version", LwjglVersion },
    };
    const QJsonObject minecraft{
        { "cachedName", "Minecraft" },
        { "cachedRequires",
          QJsonArray{ QJsonObject{ { "suggests", LwjglVersion }, { "uid", "org.lwjgl3" } } } },
        { "cachedVersion", MinecraftVersion },
        { "important", true },
        { "uid", "net.minecraft" },
        { "version", MinecraftVersion },
    };
    const QJsonObject neoforge{
        { "cachedName", "NeoForge" },
        { "cachedRequires",
          QJsonArray{ QJsonObject{ { "equals", MinecraftVersion }, { "uid", "net.minecraft" } } } },
        { "cachedVersion", NeoForgeVersion },
        { "uid", "net.neoforged" },
        { "version", NeoForgeVersion },
    };
    const QJsonObject root{
        { "components", QJsonArray{ lwjgl, minecraft, neoforge } },
        { "formatVersion", 1 },
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

}  // namespace

MinecraftInstance* InstanceSetup::find()
{
    auto* list = APPLICATION ? APPLICATION->instances() : nullptr;
    return list ? list->getInstanceById(Config::instanceId()) : nullptr;
}

bool InstanceSetup::writeInstanceFiles(const QString& root, QString* error)
{
    if (!QDir().mkpath(root)) {
        if (error)
            *error = QStringLiteral("Не удалось создать папку инстанса %1").arg(root);
        return false;
    }

    const QString configPath = QDir(root).filePath(QStringLiteral("instance.cfg"));
    const QString packPath = QDir(root).filePath(QStringLiteral("mmc-pack.json"));

    if (!QFileInfo::exists(configPath) && !writeFile(configPath, instanceConfig(), error))
        return false;
    if (!QFileInfo::exists(packPath) && !writeFile(packPath, componentManifest(), error))
        return false;

    return true;
}

MinecraftInstance* InstanceSetup::ensure(QString* error)
{
    if (auto* existing = find())
        return existing;

    const QString root = Config::instanceRoot();
    if (root.isEmpty()) {
        if (error)
            *error = QStringLiteral("Лаунчер не знает папку инстансов");
        return nullptr;
    }

    if (!writeInstanceFiles(root, error))
        return nullptr;

    auto* list = APPLICATION->instances();
    if (!list) {
        if (error)
            *error = QStringLiteral("Список инстансов недоступен");
        return nullptr;
    }

    if (list->loadList() != InstanceList::NoError) {
        if (error)
            *error = QStringLiteral("Не удалось перечитать список инстансов");
        return nullptr;
    }

    if (auto* instance = find())
        return instance;

    if (error)
        *error = QStringLiteral("Инстанс %1 не появился после создания").arg(Config::instanceId());
    return nullptr;
}

}  // namespace berry
