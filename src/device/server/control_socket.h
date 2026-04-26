#ifndef CONTROL_SOCKET_H
#define CONTROL_SOCKET_H

#include <QObject>
#include <QPointer>
#include <QTcpSocket>

class DeviceMsg;

class ControlSocket : public QObject {
    Q_OBJECT
public:
    explicit ControlSocket(QObject *parent = nullptr);
    ~ControlSocket() override;

    // Takes ownership of an existing socket (incoming connection)
    void SetSocket(QTcpSocket *socket);

    // Connect to remote host (outgoing connection)
    bool ConnectToHost(const QString &host, quint16 port, int timeoutMs);

    // Send a serialized control message
    qint64 Send(const QByteArray &data);

    // Close and free the underlying socket
    void Close();

    bool IsValid() const;
    QTcpSocket *Socket() const;

signals:
    void DeviceMessageReceived(DeviceMsg *msg);
    void Disconnected();
    void ErrorOccurred(const QString &error);

private slots:
    void OnReadyRead();
    void OnDisconnected();

private:
    QPointer<QTcpSocket> m_socket;
};

#endif  // CONTROL_SOCKET_H
