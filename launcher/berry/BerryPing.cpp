// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BerryTrip Launcher - single server build of Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "berry/BerryPing.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QtConcurrent>

#include <memory>

namespace berry {

namespace {

constexpr int ProtocolVersion = 767;  // Minecraft 1.21.1

void writeVarInt(QByteArray& out, int value)
{
    quint32 rest = static_cast<quint32>(value);
    do {
        quint8 current = rest & 0x7F;
        rest >>= 7;
        if (rest != 0)
            current |= 0x80;
        out.append(static_cast<char>(current));
    } while (rest != 0);
}

void writeString(QByteArray& out, const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    writeVarInt(out, bytes.size());
    out.append(bytes);
}

int readVarInt(QTcpSocket& socket, QString* error)
{
    int result = 0;
    int shift = 0;
    while (true) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(socket.property("berryTimeout").toInt())) {
            *error = QStringLiteral("сервер не ответил");
            return -1;
        }
        char byte = 0;
        if (socket.read(&byte, 1) != 1) {
            *error = QStringLiteral("соединение закрыто");
            return -1;
        }
        const auto value = static_cast<quint8>(byte);
        result |= (value & 0x7F) << shift;
        if ((value & 0x80) == 0)
            return result;
        shift += 7;
        if (shift > 35) {
            *error = QStringLiteral("слишком длинный varint");
            return -1;
        }
    }
}

QString flattenText(const QJsonValue& value)
{
    if (value.isNull() || value.isUndefined())
        return {};
    if (value.isString())
        return value.toString();
    if (value.isArray()) {
        QString text;
        for (const auto& part : value.toArray())
            text += flattenText(part);
        return text;
    }
    if (value.isObject()) {
        const auto object = value.toObject();
        QString text = flattenText(object.value(QStringLiteral("text")));
        text += flattenText(object.value(QStringLiteral("extra")));
        return text;
    }
    return {};
}

QString cleanMotd(QString value)
{
    static const QRegularExpression codes(QStringLiteral("\u00A7[0-9a-fk-orA-FK-OR]"));
    value.remove(codes);
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.trimmed();
}

ServerPing::Status probe(const QString& host, quint16 port, int timeoutMs)
{
    ServerPing::Status status;

    QTcpSocket socket;
    socket.setProperty("berryTimeout", timeoutMs);
    QElapsedTimer timer;
    timer.start();
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(timeoutMs)) {
        status.error = QStringLiteral("нет связи с сервером");
        return status;
    }
    status.ping = static_cast<int>(timer.elapsed());
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    QByteArray handshake;
    writeVarInt(handshake, 0x00);
    writeVarInt(handshake, ProtocolVersion);
    writeString(handshake, host);
    handshake.append(static_cast<char>((port >> 8) & 0xFF));
    handshake.append(static_cast<char>(port & 0xFF));
    writeVarInt(handshake, 1);

    QByteArray packet;
    writeVarInt(packet, handshake.size());
    packet.append(handshake);
    writeVarInt(packet, 1);
    packet.append(static_cast<char>(0x00));
    socket.write(packet);

    if (!socket.waitForBytesWritten(timeoutMs)) {
        status.error = QStringLiteral("не удалось отправить запрос");
        return status;
    }

    QString readError;
    const int length = readVarInt(socket, &readError);
    if (length <= 0 || length > 512 * 1024) {
        status.error = readError.isEmpty() ? QStringLiteral("неожиданный размер ответа") : readError;
        return status;
    }

    QByteArray payload;
    while (payload.size() < length) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeoutMs)) {
            status.error = QStringLiteral("сервер не ответил");
            return status;
        }
        payload.append(socket.read(length - payload.size()));
    }

    // payload: varint packetId(0) + varint length + json
    int offset = 0;
    auto readPayloadVarInt = [&payload, &offset]() {
        int result = 0;
        int shift = 0;
        while (offset < payload.size()) {
            const auto byte = static_cast<quint8>(payload.at(offset++));
            result |= (byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
                break;
            shift += 7;
        }
        return result;
    };

    if (readPayloadVarInt() != 0) {
        status.error = QStringLiteral("сервер ответил другим пакетом");
        return status;
    }

    const int jsonLength = readPayloadVarInt();
    if (jsonLength <= 0 || offset + jsonLength > payload.size()) {
        status.error = QStringLiteral("неверная строка в ответе");
        return status;
    }

    const auto json = QJsonDocument::fromJson(payload.mid(offset, jsonLength)).object();
    const auto players = json.value(QStringLiteral("players")).toObject();
    if (players.contains(QStringLiteral("online")))
        status.online = players.value(QStringLiteral("online")).toInt();
    if (players.contains(QStringLiteral("max")))
        status.max = players.value(QStringLiteral("max")).toInt();
    status.version = json.value(QStringLiteral("version")).toObject().value(QStringLiteral("name")).toString();
    status.motd = cleanMotd(flattenText(json.value(QStringLiteral("description"))));
    status.ok = true;
    return status;
}

}  // namespace

ServerPing::ServerPing(QObject* parent) : QObject(parent) {}

void ServerPing::query(const QString& host, quint16 port, int timeoutMs)
{
    if (m_running)
        return;
    m_running = true;

    auto* watcher = new QFutureWatcher<Status>(this);
    connect(watcher, &QFutureWatcher<Status>::finished, this, [this, watcher] {
        const Status status = watcher->result();
        watcher->deleteLater();
        m_running = false;
        emit finished(status);
    });
    watcher->setFuture(QtConcurrent::run([host, port, timeoutMs] { return probe(host, port, timeoutMs); }));
}

}  // namespace berry
