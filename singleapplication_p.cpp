// The MIT License (MIT)
//
// Copyright (C) Itay Grudev 2015 - 2021
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//
//  W A R N I N G !!!
//  -----------------
//
// This file is not part of the SingleApplication API. It is used purely as an
// implementation detail. This header file may change from version to
// version without notice, or may even be removed.
//

#include "singleapplication_p.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
#include <QThread>
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
#include <QRandomGenerator>
#else
#include <QDateTime>
#endif
#ifdef Q_OS_WINDOWS
#include <QLibrary>
#include <qt_windows.h>
#ifndef UNLEN
#define UNLEN 256 // Maximum user name length
#endif
using lpGetUserNameW = BOOL(WINAPI *)(LPWSTR, LPDWORD);
static lpGetUserNameW m_lpGetUserNameW = nullptr;
#endif
#ifdef Q_OS_UNIX
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

SingleApplicationPrivate::SingleApplicationPrivate(SingleApplication *q_ptr) : q_ptr(q_ptr) {}

SingleApplicationPrivate::~SingleApplicationPrivate()
{
    if (socket != nullptr) {
        socket->close();
        delete socket;
    }

    if (memory != nullptr) {
        memory->lock();
        auto *inst = static_cast<InstancesInfo *>(memory->data());
        if (server != nullptr) {
            server->close();
            delete server;
            inst->primary = false;
            inst->primaryPid = -1;
            inst->primaryUser[0] = '\0';
            inst->checksum = blockChecksum();
        }
        memory->unlock();

        delete memory;
    }
}

QString SingleApplicationPrivate::getUsername()
{
#ifdef Q_OS_WINDOWS
    wchar_t username[UNLEN + 1];
    // Specifies size of the buffer on input
    DWORD usernameLength = UNLEN + 1;
    if (!m_lpGetUserNameW) {
        m_lpGetUserNameW = reinterpret_cast<lpGetUserNameW>(
            QLibrary::resolve(QString::fromUtf8("Advapi32"), "GetUserNameW"));
    }
    if (m_lpGetUserNameW) {
        if (m_lpGetUserNameW(username, &usernameLength)) {
            return QString::fromWCharArray(username);
        }
    }
#if (QT_VERSION < QT_VERSION_CHECK(5, 10, 0))
    return QString::fromLocal8Bit(qgetenv("USERNAME"));
#else
    return qEnvironmentVariable("USERNAME");
#endif
#endif
#ifdef Q_OS_UNIX
    QString username;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (pw)
        username = QString::fromLocal8Bit(pw->pw_name);
    if (username.isEmpty()) {
#if (QT_VERSION < QT_VERSION_CHECK(5, 10, 0))
        username = QString::fromLocal8Bit(qgetenv("USER"));
#else
        username = qEnvironmentVariable("USER");
#endif
    }
    return username;
#endif
}

void SingleApplicationPrivate::genBlockServerName()
{
    QCryptographicHash appData(QCryptographicHash::Sha256);
    appData.addData("SingleApplication", 17);
    appData.addData(QCoreApplication::applicationName().toUtf8());
    appData.addData(QCoreApplication::organizationName().toUtf8());
    appData.addData(QCoreApplication::organizationDomain().toUtf8());

    if (!appDataList.isEmpty()) {
        appData.addData(appDataList.join(u"").toUtf8());
    }

    if (!(options & SingleApplication::Mode::ExcludeAppVersion)) {
        appData.addData(QCoreApplication::applicationVersion().toUtf8());
    }

    if (!(options & SingleApplication::Mode::ExcludeAppPath)) {
#ifdef Q_OS_WINDOWS
        appData.addData(QCoreApplication::applicationFilePath().toLower().toUtf8());
#else
        appData.addData(QCoreApplication::applicationFilePath().toUtf8());
#endif
    }

    // User level block requires a user specific data in the hash
    if (options & SingleApplication::Mode::User) {
        appData.addData(getUsername().toUtf8());
    }

    // Replace the backslash in RFC 2045 Base64 [a-zA-Z0-9+/=] to comply with
    // server naming requirements.
    blockServerName = QString::fromUtf8(appData.result().toBase64().replace("/", "_"));
}

void SingleApplicationPrivate::initializeMemoryBlock() const
{
    auto *inst = static_cast<InstancesInfo *>(memory->data());
    inst->primary = false;
    inst->secondary = 0;
    inst->primaryPid = -1;
    inst->primaryUser[0] = '\0';
    inst->checksum = blockChecksum();
}

void SingleApplicationPrivate::startPrimary()
{
    // Reset the number of connections
    auto *inst = static_cast<InstancesInfo *>(memory->data());

    inst->primary = true;
    inst->primaryPid = QCoreApplication::applicationPid();
    qstrncpy(inst->primaryUser, getUsername().toUtf8().data(), sizeof(inst->primaryUser));
    inst->checksum = blockChecksum();
    instanceNumber = 0;
    // Successful creation means that no main process exists
    // So we start a QLocalServer to listen for connections
    QLocalServer::removeServer(blockServerName);
    server = new QLocalServer();

    // Restrict access to the socket according to the
    // SingleApplication::Mode::User flag on User level or no restrictions
    if (options & SingleApplication::Mode::User) {
        server->setSocketOptions(QLocalServer::UserAccessOption);
    } else {
        server->setSocketOptions(QLocalServer::WorldAccessOption);
    }

    server->listen(blockServerName);
    connect(server, &QLocalServer::newConnection, this, &SingleApplicationPrivate::slotConnectionEstablished);
}

void SingleApplicationPrivate::startSecondary()
{
    auto *inst = static_cast<InstancesInfo *>(memory->data());

    inst->secondary += 1;
    inst->checksum = blockChecksum();
    instanceNumber = inst->secondary;
}

bool SingleApplicationPrivate::connectToPrimary(int msecs, ConnectionType connectionType)
{
    QElapsedTimer time;
    time.start();

    // Connect to the Local Server of the Primary Instance if not already
    // connected.
    if (socket == nullptr) {
        socket = new QLocalSocket();
    }

    if (socket->state() == QLocalSocket::ConnectedState)
        return true;

    if (socket->state() != QLocalSocket::ConnectedState) {
        while (true) {
            randomSleep();

            if (socket->state() != QLocalSocket::ConnectingState)
                socket->connectToServer(blockServerName);

            if (socket->state() == QLocalSocket::ConnectingState) {
                socket->waitForConnected(static_cast<int>(msecs - time.elapsed()));
            }

            // If connected break out of the loop
            if (socket->state() == QLocalSocket::ConnectedState)
                break;

            // If elapsed time since start is longer than the method timeout return
            if (time.elapsed() >= msecs)
                return false;
        }
    }

    // Initialisation message according to the SingleApplication protocol
    QByteArray initMsg;
    QDataStream writeStream(&initMsg, QIODevice::WriteOnly);

    writeStream << blockServerName.toLatin1();
    writeStream << static_cast<quint8>(connectionType);
    writeStream << instanceNumber;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    quint16 checksum = qChecksum(QByteArray(initMsg, static_cast<quint32>(initMsg.length())));
#else
    quint16 checksum = qChecksum(initMsg.constData(), static_cast<quint32>(initMsg.length()));
#endif
    writeStream << checksum;

    return writeConfirmedMessage(static_cast<int>(msecs - time.elapsed()), initMsg);
}

void SingleApplicationPrivate::writeAck(QLocalSocket *sock)
{
    sock->putChar('\n');
}

bool SingleApplicationPrivate::writeConfirmedMessage(int msecs, const QByteArray &msg)
{
    QElapsedTimer time;
    time.start();

    // Frame 1: The header indicates the message length that follows
    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);

    headerStream << static_cast<quint64>(msg.length());

    if(!writeConfirmedFrame(static_cast<int>(msecs - time.elapsed()), header))
        return false;

    // Frame 2: The message
    return writeConfirmedFrame(static_cast<int>(msecs - time.elapsed()), msg);
}

bool SingleApplicationPrivate::writeConfirmedFrame(int msecs, const QByteArray &msg)
{
    socket->write(msg);
    socket->flush();

    bool result = socket->waitForReadyRead(msecs); // await ack byte
    if (result) {
        socket->read(1);
        return true;
    }

    return false;
}

quint16 SingleApplicationPrivate::blockChecksum() const
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    quint16 checksum = qChecksum(QByteArray(static_cast<const char *>(memory->constData()),
                                            offsetof(InstancesInfo, checksum)));
#else
    quint16 checksum = qChecksum(static_cast<const char *>(memory->constData()),
                                 offsetof(InstancesInfo, checksum));
#endif
    return checksum;
}

qint64 SingleApplicationPrivate::primaryPid() const
{
    qint64 pid;

    memory->lock();
    auto *inst = static_cast<InstancesInfo *>(memory->data());
    pid = inst->primaryPid;
    memory->unlock();

    return pid;
}

QString SingleApplicationPrivate::primaryUser() const
{
    QByteArray username;

    memory->lock();
    auto *inst = static_cast<InstancesInfo *>(memory->data());
    username = inst->primaryUser;
    memory->unlock();

    return QString::fromUtf8(username);
}

/**
 * @brief Executed when a connection has been made to the LocalServer
 */
void SingleApplicationPrivate::slotConnectionEstablished()
{
    QLocalSocket *nextConnSocket = server->nextPendingConnection();
    connectionMap.insert(nextConnSocket, ConnectionInfo());

    connect(nextConnSocket, &QLocalSocket::aboutToClose, this, [nextConnSocket, this](){
        auto &info = connectionMap[nextConnSocket];
        slotClientConnectionClosed(nextConnSocket, info.instanceId);
    });

    connect(nextConnSocket, &QLocalSocket::disconnected, nextConnSocket, &QLocalSocket::deleteLater);

    connect(nextConnSocket, &QLocalSocket::destroyed, this, [nextConnSocket, this](){
        connectionMap.remove(nextConnSocket);
    });

    connect(nextConnSocket, &QLocalSocket::readyRead, this, [nextConnSocket, this](){
        auto &info = connectionMap[nextConnSocket];
        switch (static_cast<ConnectionStage>(info.stage)) {
        case ConnectionStage::StageInitHeader:
            readMessageHeader(nextConnSocket, ConnectionStage::StageInitBody);
            break;
        case ConnectionStage::StageInitBody:
            readInitMessageBody(nextConnSocket);
            break;
        case ConnectionStage::StageConnectedHeader:
            readMessageHeader(nextConnSocket, ConnectionStage::StageConnectedBody);
            break;
        case ConnectionStage::StageConnectedBody:
            slotDataAvailable(nextConnSocket, info.instanceId);
            break;
        default:
            break;
        };
    });
}

void SingleApplicationPrivate::readMessageHeader(QLocalSocket *sock, SingleApplicationPrivate::ConnectionStage nextStage)
{
    if (!connectionMap.contains(sock)) {
        return;
    }

    if (sock->bytesAvailable() < static_cast<qint64>(sizeof(quint64))) {
        return;
    }

    QDataStream headerStream(sock);

    // Read the header to know the message length
    quint64 msgLen = 0;
    headerStream >> msgLen;
    ConnectionInfo &info = connectionMap[sock];
    info.stage = static_cast<quint8>(nextStage);
    info.msgLen = msgLen;

    writeAck(sock);
}

bool SingleApplicationPrivate::isFrameComplete(QLocalSocket *sock)
{
    if (!connectionMap.contains(sock)) {
        return false;
    }

    ConnectionInfo &info = connectionMap[sock];
    if (sock->bytesAvailable() < static_cast<qint64>(info.msgLen)) {
        return false;
    }

    return true;
}

void SingleApplicationPrivate::readInitMessageBody(QLocalSocket *sock)
{
    Q_Q(SingleApplication);

    if(!isFrameComplete(sock))
        return;

    // Read the message body
    QByteArray msgBytes = sock->readAll();
    QDataStream readStream(msgBytes);

    // server name
    QByteArray latin1Name;
    readStream >> latin1Name;

    // connection type
    ConnectionType connectionType = ConnectionType::InvalidConnection;
    quint8 connTypeVal = static_cast<quint8>(ConnectionType::InvalidConnection);
    readStream >> connTypeVal;
    connectionType = static_cast<ConnectionType>(connTypeVal);

    // instance id
    quint32 instanceId = 0;
    readStream >> instanceId;

    // checksum
    quint16 msgChecksum = 0;
    readStream >> msgChecksum;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    const quint16 actualChecksum = qChecksum(QByteArray(msgBytes, static_cast<quint32>(msgBytes.length() - sizeof(quint16))));
#else
    const quint16 actualChecksum = qChecksum(msgBytes.constData(), static_cast<quint32>(msgBytes.length() - sizeof(quint16)));
#endif

    bool isValid = readStream.status() == QDataStream::Ok && QLatin1String(latin1Name) == blockServerName && msgChecksum == actualChecksum;

    if (!isValid) {
        sock->close();
        return;
    }

    ConnectionInfo &info = connectionMap[sock];
    info.instanceId = instanceId;
    info.stage = static_cast<quint8>(ConnectionStage::StageConnectedHeader);

    if (connectionType == ConnectionType::NewInstance
        || (connectionType == ConnectionType::SecondaryInstance
            && options & SingleApplication::Mode::SecondaryNotification)) {
        Q_EMIT q->instanceStarted();
    }

    writeAck(sock);
}

void SingleApplicationPrivate::slotDataAvailable(QLocalSocket *dataSocket, quint32 instanceId)
{
    Q_Q(SingleApplication);

    if (!isFrameComplete(dataSocket))
        return;

    Q_EMIT q->receivedMessage(instanceId, dataSocket->readAll());

    writeAck(dataSocket);

    ConnectionInfo &info = connectionMap[dataSocket];
    info.stage = static_cast<quint8>(ConnectionStage::StageConnectedHeader);
}

void SingleApplicationPrivate::slotClientConnectionClosed(QLocalSocket *closedSocket,
                                                          quint32 instanceId)
{
    if (closedSocket->bytesAvailable() > 0)
        slotDataAvailable(closedSocket, instanceId);
}

void SingleApplicationPrivate::randomSleep()
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    QThread::msleep(QRandomGenerator::global()->bounded(8u, 18u));
#else
    qsrand(QDateTime::currentMSecsSinceEpoch() % std::numeric_limits<uint>::max());
    QThread::msleep(qrand() % 11 + 8);
#endif
}

void SingleApplicationPrivate::addAppData(const QString &data)
{
    appDataList.push_back(data);
}

QStringList SingleApplicationPrivate::appData() const
{
    return appDataList;
}
