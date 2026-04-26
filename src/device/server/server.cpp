#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QTimerEvent>

#include "server.h"

#define DEVICE_NAME_FIELD_LENGTH 64
#define SOCKET_NAME_PREFIX "scrcpy"
#define MAX_CONNECT_COUNT 30
#define MAX_RESTART_COUNT 1

quint32 Server::BufferRead32be(const quint8 *buf) {
    return static_cast<quint32>((buf[0] << 24) | (buf[1] << 16) |
                                (buf[2] << 8) | buf[3]);
}

QString Server::SocketName() const {
    return QString(SOCKET_NAME_PREFIX "_%1")
        .arg(m_params.scid, 8, 16, QChar('0'));
}

Server::Server(QObject *parent) : QObject(parent) {
    connect(&m_workProcess, &qsc::AdbProcess::adbProcessResult, this,
            &Server::OnWorkProcessResult);
    connect(&m_serverProcess, &qsc::AdbProcess::adbProcessResult, this,
            &Server::OnWorkProcessResult);

    connect(&m_acceptor, &QTcpServer::newConnection, this,
            &Server::OnNewConnection);
}

Server::~Server() {}

bool Server::Start(ServerParams params) {
    m_params = params;
    m_serverStartStep = SSS_PUSH;
    m_expectingVideoSocket = true;
    return StartServerByStep();
}

void Server::Stop() {
    StopAcceptTimeout();
    StopConnectTimer();

    if (m_controlSocket) {
        m_controlSocket->Close();
        m_controlSocket = nullptr;
    }

    m_serverProcess.kill();

    if (m_tunnelEnabled) {
        if (m_tunnelForward) {
            DisableTunnelForward();
        } else {
            DisableTunnelReverse();
        }
        m_tunnelForward = false;
        m_tunnelEnabled = false;
    }
    m_acceptor.Close();
}

bool Server::IsReverse() const { return !m_tunnelForward; }

Server::ServerParams Server::GetParams() const { return m_params; }

VideoSocket *Server::RemoveVideoSocket() {
    VideoSocket *socket = m_videoSocket;
    m_videoSocket = nullptr;
    return socket;
}

ControlSocket *Server::GetControlSocket() const { return m_controlSocket; }

// ---------------------------------------------------------------------------
// Tunnel lifecycle
// ---------------------------------------------------------------------------

bool Server::PushServer() {
    if (m_workProcess.isRuning()) m_workProcess.kill();
    m_workProcess.push(m_params.serial, m_params.serverLocalPath,
                       m_params.serverRemotePath);
    return true;
}

bool Server::EnableTunnelReverse() {
    if (m_workProcess.isRuning()) m_workProcess.kill();
    m_workProcess.reverse(m_params.serial, SocketName(),
                          m_params.localPort);
    return true;
}

void Server::DisableTunnelReverse() {
    auto *adb = new qsc::AdbProcess(this);
    connect(adb, &qsc::AdbProcess::adbProcessResult, adb,
            &QObject::deleteLater);
    adb->reverseRemove(m_params.serial, SocketName());
}

bool Server::EnableTunnelForward() {
    if (m_workProcess.isRuning()) m_workProcess.kill();
    m_workProcess.forward(m_params.serial, m_params.localPort,
                          SocketName());
    return true;
}

void Server::DisableTunnelForward() {
    auto *adb = new qsc::AdbProcess(this);
    connect(adb, &qsc::AdbProcess::adbProcessResult, adb,
            &QObject::deleteLater);
    adb->forwardRemove(m_params.serial, m_params.localPort);
}

bool Server::Execute() {
    if (m_serverProcess.isRuning()) m_serverProcess.kill();

    QStringList args;
    args << "shell"
         << QString("CLASSPATH=%1").arg(m_params.serverRemotePath)
         << "app_process"
         << "/"
         << "com.genymobile.scrcpy.Server"
         << m_params.serverVersion
         << QString("video_bit_rate=%1").arg(m_params.bitRate);

    if (!m_params.logLevel.isEmpty())
        args << QString("log_level=%1").arg(m_params.logLevel);
    if (m_params.maxSize > 0)
        args << QString("max_size=%1").arg(m_params.maxSize);
    if (m_params.maxFps > 0)
        args << QString("max_fps=%1").arg(m_params.maxFps);

    if (1 == m_params.captureOrientationLock)
        args << QString("capture_orientation=@%1")
                    .arg(m_params.captureOrientation);
    else if (2 == m_params.captureOrientationLock)
        args << QString("capture_orientation=@");
    else
        args << QString("capture_orientation=%1")
                    .arg(m_params.captureOrientation);

    if (m_tunnelForward) args << "tunnel_forward=true";
    if (!m_params.crop.isEmpty()) args << QString("crop=%1").arg(m_params.crop);
    if (!m_params.control) args << "control=false";
    if (m_params.stayAwake) args << "stay_awake=true";
    if (!m_params.codecOptions.isEmpty())
        args << QString("codec_options=%1").arg(m_params.codecOptions);
    if (!m_params.codecName.isEmpty())
        args << QString("encoder_name=%1").arg(m_params.codecName);

    args << "audio=false";
    if (-1 != m_params.scid)
        args << QString("scid=%1").arg(m_params.scid, 8, 16, QChar('0'));

    m_serverProcess.execute(m_params.serial, args);
    return true;
}

// ---------------------------------------------------------------------------
// Step-by-step state machine
// ---------------------------------------------------------------------------

bool Server::StartServerByStep() {
    if (SSS_NULL == m_serverStartStep) return false;

    bool ok = false;
    switch (m_serverStartStep) {
    case SSS_PUSH:                ok = PushServer();          break;
    case SSS_ENABLE_TUNNEL_REVERSE: ok = EnableTunnelReverse(); break;
    case SSS_ENABLE_TUNNEL_FORWARD: ok = EnableTunnelForward(); break;
    case SSS_EXECUTE_SERVER:      ok = Execute();             break;
    default:                                                   break;
    }

    if (!ok) emit ServerStarted(false);
    return ok;
}

void Server::ConnectToDevice() {
    if (SSS_RUNNING != m_serverStartStep) {
        qWarning("server not running");
        return;
    }
    if (!m_tunnelForward && !m_videoSocket) {
        StartAcceptTimeout();
    } else {
        StartConnectTimer();
    }
}

// ---------------------------------------------------------------------------
// Incoming connection handler (reverse mode)
// ---------------------------------------------------------------------------

void Server::OnNewConnection() {
    while (m_acceptor.hasPendingConnections()) {
        QTcpSocket *socket = m_acceptor.nextPendingConnection();

        if (auto *video = dynamic_cast<VideoSocket *>(socket)) {
            m_videoSocket = video;
            if (!m_videoSocket->isValid() ||
                !ReadDeviceInfo(m_videoSocket, m_deviceName,
                                m_deviceSize)) {
                Stop();
                emit ServerStarted(false);
            }
        } else {
            auto *ctrl = new ControlSocket();
            ctrl->SetSocket(socket);
            m_controlSocket = ctrl;

            if (m_controlSocket->IsValid()) {
                m_acceptor.Close();
                DisableTunnelReverse();
                m_tunnelEnabled = false;
                emit ServerStarted(true, m_deviceName, m_deviceSize);
            } else {
                Stop();
                emit ServerStarted(false);
            }
            StopAcceptTimeout();
        }
    }
}

// ---------------------------------------------------------------------------
// Device info reading
// ---------------------------------------------------------------------------

bool Server::ReadDeviceInfo(VideoSocket *socket, QString &deviceName,
                             QSize &size) {
    QElapsedTimer timer;
    timer.start();
    unsigned char buf[DEVICE_NAME_FIELD_LENGTH + 12];

    while (socket->bytesAvailable() <
           static_cast<qint64>(DEVICE_NAME_FIELD_LENGTH + 12)) {
        socket->waitForReadyRead(300);
        if (timer.elapsed() > 3000) {
            qInfo("ReadDeviceInfo timeout");
            return false;
        }
    }
    qDebug() << "ReadDeviceInfo wait time:" << timer.elapsed();

    qint64 len = socket->read(reinterpret_cast<char *>(buf), sizeof(buf));
    if (len < DEVICE_NAME_FIELD_LENGTH + 12) {
        qInfo("Could not retrieve device information");
        return false;
    }
    buf[DEVICE_NAME_FIELD_LENGTH - 1] = '\0';
    deviceName = QString::fromUtf8(reinterpret_cast<const char *>(buf));
    size.setWidth(
        BufferRead32be(&buf[DEVICE_NAME_FIELD_LENGTH + 4]));
    size.setHeight(
        BufferRead32be(&buf[DEVICE_NAME_FIELD_LENGTH + 8]));
    return true;
}

// ---------------------------------------------------------------------------
// Timeout management
// ---------------------------------------------------------------------------

void Server::StartAcceptTimeout() {
    StopAcceptTimeout();
    m_acceptTimerId = startTimer(1000);
}

void Server::StopAcceptTimeout() {
    if (m_acceptTimerId) {
        killTimer(m_acceptTimerId);
        m_acceptTimerId = 0;
    }
}

void Server::StartConnectTimer() {
    StopConnectTimer();
    m_connectTimerId = startTimer(300);
}

void Server::StopConnectTimer() {
    if (m_connectTimerId) {
        killTimer(m_connectTimerId);
        m_connectTimerId = 0;
    }
    m_connectCount = 0;
}

void Server::timerEvent(QTimerEvent *event) {
    if (!event) return;
    if (m_acceptTimerId == event->timerId()) {
        OnAcceptTimeout();
    } else if (m_connectTimerId == event->timerId()) {
        OnConnectTimer();
    }
}

void Server::OnAcceptTimeout() {
    StopAcceptTimeout();
    emit ServerStarted(false);
}

// ---------------------------------------------------------------------------
// Forward-mode connection timer (retry loop)
// ---------------------------------------------------------------------------

void Server::OnConnectTimer() {
    QString deviceName;
    QSize deviceSize;
    bool success = false;

    auto *videoSocket = new VideoSocket();
    auto *controlSocket = new ControlSocket();

    videoSocket->connectToHost(QHostAddress::LocalHost, m_params.localPort);
    if (!videoSocket->waitForConnected(1000)) {
        m_connectCount = MAX_CONNECT_COUNT;
        qWarning("video socket connect failed");
        goto cleanup;
    }

    if (!controlSocket->ConnectToHost(
            QHostAddress(QHostAddress::LocalHost).toString(),
            m_params.localPort, 1000)) {
        m_connectCount = MAX_CONNECT_COUNT;
        qWarning("control socket connect failed");
        goto cleanup;
    }

    if (videoSocket->state() != QTcpSocket::ConnectedState) {
        qWarning("video socket not connected");
        m_connectCount = MAX_CONNECT_COUNT;
        goto cleanup;
    }

    videoSocket->waitForReadyRead(1000);
    if (!videoSocket->read(1).isEmpty() &&
        ReadDeviceInfo(videoSocket, deviceName, deviceSize)) {
        success = true;
        goto cleanup;
    }

    qWarning("read device info failed, retrying...");

cleanup:
    if (success) {
        StopConnectTimer();
        m_videoSocket = videoSocket;
        controlSocket->Socket()->read(1);  // consume dummy byte
        m_controlSocket = controlSocket;
        DisableTunnelForward();
        m_tunnelEnabled = false;
        m_restartCount = 0;
        emit ServerStarted(true, deviceName, deviceSize);
        return;
    }

    if (videoSocket) videoSocket->deleteLater();
    if (controlSocket) controlSocket->deleteLater();

    if (++m_connectCount >= MAX_CONNECT_COUNT) {
        StopConnectTimer();
        Stop();
        if (m_restartCount++ < MAX_RESTART_COUNT) {
            qWarning("restarting server...");
            Start(m_params);
        } else {
            m_restartCount = 0;
            emit ServerStarted(false);
        }
    }
}

// ---------------------------------------------------------------------------
// ADB process result handler
// ---------------------------------------------------------------------------

void Server::OnWorkProcessResult(
    qsc::AdbProcess::ADB_EXEC_RESULT result) {
    if (sender() == &m_workProcess) {
        if (SSS_NULL == m_serverStartStep) return;

        switch (m_serverStartStep) {
        case SSS_PUSH:
            if (qsc::AdbProcess::AER_SUCCESS_EXEC == result) {
                m_serverStartStep = m_params.useReverse
                                        ? SSS_ENABLE_TUNNEL_REVERSE
                                        : SSS_ENABLE_TUNNEL_FORWARD;
                if (!m_params.useReverse) m_tunnelForward = true;
                StartServerByStep();
            } else if (qsc::AdbProcess::AER_SUCCESS_START != result) {
                qCritical("adb push failed");
                m_serverStartStep = SSS_NULL;
                emit ServerStarted(false);
            }
            break;

        case SSS_ENABLE_TUNNEL_REVERSE:
            if (qsc::AdbProcess::AER_SUCCESS_EXEC == result) {
                if (!m_acceptor.Listen(
                        QHostAddress::LocalHost, m_params.localPort,
                        [this](qintptr handle) -> QTcpSocket * {
                            if (m_expectingVideoSocket) {
                                auto *video = new VideoSocket();
                                video->setSocketDescriptor(handle);
                                m_expectingVideoSocket = false;
                                return video;
                            }
                            auto *raw = new QTcpSocket();
                            raw->setSocketDescriptor(handle);
                            return raw;
                        })) {
                    m_serverStartStep = SSS_NULL;
                    DisableTunnelReverse();
                    emit ServerStarted(false);
                    break;
                }
                m_serverStartStep = SSS_EXECUTE_SERVER;
                StartServerByStep();
            } else if (qsc::AdbProcess::AER_SUCCESS_START != result) {
                qCritical("adb reverse failed, falling back to forward");
                m_tunnelForward = true;
                m_serverStartStep = SSS_ENABLE_TUNNEL_FORWARD;
                StartServerByStep();
            }
            break;

        case SSS_ENABLE_TUNNEL_FORWARD:
            if (qsc::AdbProcess::AER_SUCCESS_EXEC == result) {
                m_serverStartStep = SSS_EXECUTE_SERVER;
                StartServerByStep();
            } else if (qsc::AdbProcess::AER_SUCCESS_START != result) {
                qCritical("adb forward failed");
                m_serverStartStep = SSS_NULL;
                emit ServerStarted(false);
            }
            break;

        default:
            break;
        }
    }

    if (sender() == &m_serverProcess) {
        if (SSS_EXECUTE_SERVER == m_serverStartStep) {
            if (qsc::AdbProcess::AER_SUCCESS_START == result) {
                m_serverStartStep = SSS_RUNNING;
                m_tunnelEnabled = true;
                ConnectToDevice();
            } else if (qsc::AdbProcess::AER_ERROR_START == result) {
                if (!m_tunnelForward) {
                    m_acceptor.Close();
                    DisableTunnelReverse();
                } else {
                    DisableTunnelForward();
                }
                qCritical("adb shell start server failed");
                m_serverStartStep = SSS_NULL;
                emit ServerStarted(false);
            }
        } else if (SSS_RUNNING == m_serverStartStep) {
            m_serverStartStep = SSS_NULL;
            emit ServerStopped();
        }
    }
}
