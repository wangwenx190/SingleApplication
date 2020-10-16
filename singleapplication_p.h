// The MIT License (MIT)
//
// Copyright (C) Itay Grudev 2015 - 2020
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

#pragma once

#include "singleapplication.h"

QT_FORWARD_DECLARE_CLASS(QSharedMemory)
QT_FORWARD_DECLARE_CLASS(QLocalServer)
QT_FORWARD_DECLARE_CLASS(QLocalSocket)

struct InstancesInfo
{
    bool primary;
    quint32 secondary;
    qint64 primaryPid;
    char primaryUser[128];
    quint16 checksum; // Must be the last field
};

struct ConnectionInfo
{
    qint64 msgLen = 0;
    quint32 instanceId = 0;
    quint8 stage = 0;
};

class SingleApplicationPrivate : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SingleApplicationPrivate)
    Q_DECLARE_PUBLIC(SingleApplication)

public:
    enum class ConnectionType : quint8 {
        InvalidConnection = 0,
        NewInstance = 1,
        SecondaryInstance = 2,
        Reconnect = 3
    };
    Q_ENUM(ConnectionType)

    enum class ConnectionStage : quint8 {
        StageHeader = 0,
        StageBody = 1,
        StageConnected = 2,
    };
    Q_ENUM(ConnectionStage)

    explicit SingleApplicationPrivate(SingleApplication *q_ptr);
    ~SingleApplicationPrivate() override;

    QString getUsername();
    void genBlockServerName();
    void initializeMemoryBlock();
    void startPrimary();
    void startSecondary();
    bool connectToPrimary(int msecs, ConnectionType connectionType);
    quint16 blockChecksum();
    qint64 primaryPid();
    QString primaryUser();
    void readInitMessageHeader(QLocalSocket *socket);
    void readInitMessageBody(QLocalSocket *socket);
    void randomSleep();

    SingleApplication *q_ptr = nullptr;
    QSharedMemory *memory = nullptr;
    QLocalSocket *socket = nullptr;
    QLocalServer *server = nullptr;
    quint32 instanceNumber = 0;
    QString blockServerName = {};
    SingleApplication::Options options = {};
    QMap<QLocalSocket *, ConnectionInfo> connectionMap = {};

public Q_SLOTS:
    void slotConnectionEstablished();
    void slotDataAvailable(QLocalSocket *, quint32);
    void slotClientConnectionClosed(QLocalSocket *, quint32);
};
