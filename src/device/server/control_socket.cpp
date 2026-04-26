#include "control_socket.h"

#include <QDebug>
#include <QHostAddress>

#include "../controller/receiver/devicemsg.h"

ControlSocket::ControlSocket(QObject *parent) : QObject(parent) {}

ControlSocket::~ControlSocket() { Close(); }

void ControlSocket::SetSocket(QTcpSocket *socket) {
    if (!socket) return;
    Close();
    m_socket = socket;
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this,
            &ControlSocket::OnReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this,
            &ControlSocket::OnDisconnected);
}

bool ControlSocket::ConnectToHost(const QString &host, quint16 port,
                                   int timeoutMs) {
    Close();

    auto *socket = new QTcpSocket(this);
    socket->connectToHost(QHostAddress(host), port);
    if (!socket->waitForConnected(timeoutMs)) {
        delete socket;
        qWarning("ControlSocket: connect to %s:%d failed", qPrintable(host),
                 port);
        return false;
    }

    m_socket = socket;
    connect(m_socket, &QTcpSocket::readyRead, this,
            &ControlSocket::OnReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this,
            &ControlSocket::OnDisconnected);
    return true;
}

qint64 ControlSocket::Send(const QByteArray &data) {
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState) {
        return -1;
    }
    return m_socket->write(data.data(), data.size());
}

void ControlSocket::Close() {
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

bool ControlSocket::IsValid() const {
    return m_socket && m_socket->isValid();
}

QTcpSocket *ControlSocket::Socket() const { return m_socket; }

void ControlSocket::OnReadyRead() {
    if (!m_socket) return;

    while (m_socket->bytesAvailable() > 0) {
        QByteArray data = m_socket->peek(m_socket->bytesAvailable());
        DeviceMsg msg;
        qint32 consumed = msg.deserialize(data);
        if (consumed <= 0) break;
        m_socket->read(consumed);
        emit DeviceMessageReceived(&msg);
    }
}

void ControlSocket::OnDisconnected() {
    qDebug() << "ControlSocket: disconnected";
    emit Disconnected();
}
