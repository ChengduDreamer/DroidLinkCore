#include "connection_acceptor.h"

#include <QDebug>
#include <QTcpSocket>

ConnectionAcceptor::ConnectionAcceptor(QObject *parent) : QTcpServer(parent) {}

ConnectionAcceptor::~ConnectionAcceptor() { Close(); }

bool ConnectionAcceptor::Listen(const QHostAddress &address, quint16 port,
                                 SocketFactory factory) {
    if (!factory) {
        qWarning("ConnectionAcceptor: null factory");
        return false;
    }
    m_factory = std::move(factory);
    setMaxPendingConnections(2);
    if (!listen(address, port)) {
        qCritical() << "ConnectionAcceptor: Could not listen on port" << port;
        return false;
    }
    return true;
}

void ConnectionAcceptor::Close() {
    close();
    m_factory = {};
}

bool ConnectionAcceptor::IsListening() const { return isListening(); }

void ConnectionAcceptor::incomingConnection(qintptr handle) {
    if (!m_factory) {
        return;
    }
    QTcpSocket *socket = m_factory(handle);
    if (socket) {
        addPendingConnection(socket);
    }
}
