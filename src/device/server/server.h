#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QPointer>
#include <QSize>

#include "QtScrcpyCoreDef.h"
#include "adbprocess.h"
#include "connection_acceptor.h"
#include "control_socket.h"
#include "videosocket.h"

class Server : public QObject {
    Q_OBJECT

    enum ServerStartStep { SSS_NULL, SSS_PUSH, SSS_ENABLE_TUNNEL_REVERSE,
                           SSS_ENABLE_TUNNEL_FORWARD, SSS_EXECUTE_SERVER,
                           SSS_RUNNING };

public:
    struct ServerParams {
        QString serial = "";
        QString serverLocalPath = "";
        QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";
        quint16 localPort = 27183;
        quint16 maxSize = 720;
        quint32 bitRate = 8000000;
        quint32 maxFps = 0;
        bool useReverse = true;
        int captureOrientationLock = 0;
        int captureOrientation = 0;
        int stayAwake = false;
        QString serverVersion = qsc::ScrcpyServerVersion::value();
        QString logLevel = "debug";
        QString codecOptions = "";
        QString codecName = "";
        QString crop = "";
        bool control = true;
        qint32 scid = -1;
    };

    explicit Server(QObject *parent = nullptr);
    ~Server() override;

    bool Start(ServerParams params);
    void Stop();
    bool IsReverse() const;
    ServerParams GetParams() const;

    VideoSocket *RemoveVideoSocket();
    ControlSocket *GetControlSocket() const;

signals:
    void ServerStarted(bool success, const QString &deviceName = "",
                       const QSize &size = QSize());
    void ServerStopped();

private slots:
    void OnWorkProcessResult(qsc::AdbProcess::ADB_EXEC_RESULT processResult);

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    // Tunnel management
    bool PushServer();
    bool EnableTunnelReverse();
    void DisableTunnelReverse();
    bool EnableTunnelForward();
    void DisableTunnelForward();
    bool Execute();
    bool StartServerByStep();

    // Connection management
    void ConnectToDevice();
    bool ReadDeviceInfo(VideoSocket *socket, QString &deviceName,
                        QSize &size);
    void StartAcceptTimeout();
    void StopAcceptTimeout();
    void StartConnectTimer();
    void StopConnectTimer();
    void OnAcceptTimeout();
    void OnConnectTimer();

    // incoming connections (reverse mode)
    void OnNewConnection();

    // helper
    static quint32 BufferRead32be(const quint8 *buf);
    QString SocketName() const;

private:
    qsc::AdbProcess m_workProcess;
    qsc::AdbProcess m_serverProcess;

    // Connection infrastructure
    ConnectionAcceptor m_acceptor;       // reverse mode listener
    QPointer<VideoSocket> m_videoSocket;
    QPointer<ControlSocket> m_controlSocket;

    // State
    bool m_tunnelEnabled = false;
    bool m_tunnelForward = false;
    bool m_expectingVideoSocket = true;  // toggles between video/control
    int m_acceptTimerId = 0;
    int m_connectTimerId = 0;
    quint32 m_connectCount = 0;
    quint32 m_restartCount = 0;

    QString m_deviceName;
    QSize m_deviceSize;
    ServerParams m_params;
    ServerStartStep m_serverStartStep = SSS_NULL;
};

#endif  // SERVER_H
