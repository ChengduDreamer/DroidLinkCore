#ifndef CONNECTION_ACCEPTOR_H
#define CONNECTION_ACCEPTOR_H

#include <QTcpServer>
#include <functional>

class QTcpSocket;

class ConnectionAcceptor : public QTcpServer {
    Q_OBJECT
public:
    using SocketFactory = std::function<QTcpSocket *(qintptr handle)>;

    explicit ConnectionAcceptor(QObject *parent = nullptr);
    ~ConnectionAcceptor() override;

    bool Listen(const QHostAddress &address, quint16 port,
                SocketFactory factory);
    void Close();
    bool IsListening() const;

protected:
    void incomingConnection(qintptr handle) override;

private:
    SocketFactory m_factory;
};

#endif  // CONNECTION_ACCEPTOR_H
