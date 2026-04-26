#include <QDir>
#include <QMessageBox>
#include <QTimer>

#include "controller.h"
#include "control_socket.h"
#include "devicemsg.h"
#include "decoder.h"
#include "device.h"
#include "filehandler.h"
#include "recorder.h"
#include "server.h"
#include "demuxer.h"

namespace qsc {

Device::Device(DeviceParams params, QObject *parent) : IDevice(parent), m_params(params)
{
    if (!params.display && !m_params.recordFile) {
        qCritical("not display must be recorded");
        return;
    }

    if (params.display) {
        m_decoder = new Decoder([this](int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV) {
            for (const auto& item : m_deviceObservers) {
                item->onFrame(width, height, dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
            }
        }, this);
    m_fileHandler = new FileHandler(this);
    m_controller = new Controller([this](const QByteArray& buffer) -> qint64 {
        if (!m_server || !m_server->GetControlSocket()) {
            return 0;
        }
        return m_server->GetControlSocket()->Send(buffer);
    }, params.gameScript, this);
    }

    m_stream = new Demuxer(this);

    m_server = new Server(this);
    if (m_params.recordFile && !m_params.recordPath.trimmed().isEmpty()) {
        QString absFilePath;
        QString fileDir(m_params.recordPath);
        if (!fileDir.isEmpty()) {
            QDateTime dateTime = QDateTime::currentDateTime();
            QString fileName = dateTime.toString("_yyyyMMdd_hhmmss_zzz");
            fileName = m_params.serial + fileName;
            fileName.replace(":", "_");
            fileName.replace(".", "_");
            fileName += ("." + m_params.recordFileFormat);
            QDir dir(fileDir);
            if (!dir.exists()) {
                if (!dir.mkpath(fileDir)) {
                    qCritical() << QString("Failed to create the save folder: %1").arg(fileDir);
                }
            }
            absFilePath = dir.absoluteFilePath(fileName);
        }
        m_recorder = new Recorder(absFilePath, this);
    }
    initSignals();
}

Device::~Device()
{
    Device::disconnectDevice();
}

void Device::setUserData(void *data)
{
    m_userData = data;
}

void *Device::getUserData()
{
    return m_userData;
}

void Device::registerDeviceObserver(DeviceObserver *observer)
{
    m_deviceObservers.insert(observer);
}

void Device::deRegisterDeviceObserver(DeviceObserver *observer)
{
    m_deviceObservers.erase(observer);
}

const QString &Device::getSerial()
{
    return m_params.serial;
}

void Device::updateScript(QString script)
{
    if (m_controller) {
        m_controller->UpdateScript(script);
    }
}

void Device::screenshot()
{
    if (!m_decoder) {
        return;
    }

    // screenshot
    m_decoder->PeekFrame([this](int width, int height, uint8_t* dataRGB32) {
       saveFrame(width, height, dataRGB32);
    });
}

void Device::showTouch(bool show) {
    auto *adb = new qsc::AdbProcess();
    connect(adb, &qsc::AdbProcess::adbProcessResult, adb,
            &QObject::deleteLater);
    adb->setShowTouchesEnabled(getSerial(), show);
    qInfo() << getSerial() << " show touch "
            << (show ? "enable" : "disable");
}

bool Device::isReversePort(quint16 port) {
    if (m_server && m_server->IsReverse() && port == m_server->GetParams().localPort) {
        return true;
    }

    return false;
}

void Device::initSignals()
{
    if (m_controller) {
        connect(m_controller, &Controller::GrabCursor, this, [this](bool grab){
            for (const auto& item : m_deviceObservers) {
                item->grabCursor(grab);
            }
        });
    }
    if (m_fileHandler) {
        connect(m_fileHandler, &FileHandler::fileHandlerResult, this, [this](FileHandler::FILE_HANDLER_RESULT processResult, bool isApk) {
            QString tipsType = "";
            if (isApk) {
                tipsType = "install apk";
            } else {
                tipsType = "file transfer";
            }
            QString tips;
            if (FileHandler::FAR_IS_RUNNING == processResult) {
                tips = QString("wait current %1 to complete").arg(tipsType);
            }
            if (FileHandler::FAR_SUCCESS_EXEC == processResult) {
                tips = QString("%1 complete, save in %2").arg(tipsType).arg(m_params.pushFilePath);
            }
            if (FileHandler::FAR_ERROR_EXEC == processResult) {
                tips = QString("%1 failed").arg(tipsType);
            }
            qInfo() << tips;
        });
    }

    if (m_server) {
        connect(m_server, &Server::ServerStarted, this, [this](bool success, const QString &deviceName, const QSize &size) {
            m_serverStartSuccess = success;
            emit deviceConnected(success, m_params.serial, deviceName, size);
            if (success) {
                double diff = m_startTimeCount.elapsed() / 1000.0;
                qInfo() << QString("server start finish in %1s").arg(diff).toStdString().c_str();

                if (m_recorder) {
                    m_recorder->setFrameSize(size);
                    if (!m_recorder->open()) {
                        qCritical("Could not open recorder");
                    }
                    if (!m_recorder->startRecorder()) {
                        qCritical("Could not start recorder");
                    }
                }

                if (m_decoder) {
                    m_decoder->Open();
                }

                m_stream->InstallVideoSocket(m_server->RemoveVideoSocket());
                m_stream->SetFrameSize(size);
                m_stream->StartDecode();

                // Control socket message reception
                if (auto *ctrl = m_server->GetControlSocket()) {
                    connect(ctrl, &ControlSocket::DeviceMessageReceived, this,
                            [this](DeviceMsg *msg) {
                                if (m_controller) {
                                    m_controller->RecvDeviceMsg(msg);
                                }
                            });
                }

                if (m_params.closeScreen && m_params.display && m_controller) {
                    m_controller->SetDisplayPower(false);
                }
            } else {
                m_server->Stop();
            }
        });
        connect(m_server, &Server::ServerStopped, this, [this]() {
            disconnectDevice();
            qDebug() << "server process stop";
        });
    }

    if (m_stream) {
        connect(m_stream, &Demuxer::OnStreamStop, this, [this]() {
            disconnectDevice();
            qDebug() << "stream thread stop";
        });
        connect(m_stream, &Demuxer::GetFrame, this, [this](AVPacket *packet) {
            if (m_decoder && !m_decoder->Push(packet)) {
                qCritical("Could not send packet to decoder");
            }

            if (m_recorder && !m_recorder->push(packet)) {
                qCritical("Could not send packet to recorder");
            }
        }, Qt::DirectConnection);
        connect(m_stream, &Demuxer::GetConfigFrame, this, [this](AVPacket *packet) {
            if (m_recorder && !m_recorder->push(packet)) {
                qCritical("Could not send config packet to recorder");
            }
        }, Qt::DirectConnection);
    }

    if (m_decoder) {
        connect(m_decoder, &Decoder::UpdateFps, this, [this](quint32 fps) {
            for (const auto& item : m_deviceObservers) {
                item->updateFPS(fps);
            }
        });
    }
}

bool Device::connectDevice()
{
    if (!m_server || m_serverStartSuccess) {
        return false;
    }

    // fix: macos cant recv finished signel, timer is ok
    QTimer::singleShot(0, this, [this]() {
        m_startTimeCount.start();
        // max size support 480p 720p 1080p 设备原生分辨�?
        // support wireless connect, example:
        //m_server->start("192.168.0.174:5555", 27183, m_maxSize, m_bitRate, "");
        // only one devices, serial can be null
        // mark: crop input format: "width:height:x:y" or "" for no crop, for example: "100:200:0:0"
        Server::ServerParams params;
        params.serverLocalPath = m_params.serverLocalPath;
        params.serverRemotePath = m_params.serverRemotePath;
        params.serial = m_params.serial;
        params.localPort = m_params.localPort;
        params.maxSize = m_params.maxSize;
        params.bitRate = m_params.bitRate;
        params.maxFps = m_params.maxFps;
        params.useReverse = m_params.useReverse;
        params.captureOrientationLock = m_params.captureOrientationLock;
        params.captureOrientation = m_params.captureOrientation;
        params.stayAwake = m_params.stayAwake;
        params.serverVersion = m_params.serverVersion;
        params.logLevel = m_params.logLevel;
        params.codecOptions = m_params.codecOptions;
        params.codecName = m_params.codecName;
        params.scid = m_params.scid;

        params.crop = "";
        params.control = true;
        m_server->Start(params);
    });

    return true;
}

void Device::disconnectDevice() {
    if (!m_server) {
        return;
    }
    m_server->Stop();
    m_server->disconnect(this);
    m_server->deleteLater();
    m_server = nullptr;

    if (m_stream) {
        m_stream->StopDecode();
    }

    // server must stop before decoder, because decoder block main thread
    if (m_decoder) {
        m_decoder->Close();
    }

    if (m_recorder) {
        if (m_recorder->isRunning()) {
            m_recorder->stopRecorder();
            m_recorder->wait();
        }
        m_recorder->close();
    }

    if (m_serverStartSuccess) {
        emit deviceDisconnected(m_params.serial);
    }
    m_serverStartSuccess = false;
}

void Device::postGoBack()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostGoBack();

    for (const auto& item : m_deviceObservers) {
        item->postGoBack();
    }
}

void Device::postGoHome()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostGoHome();

    for (const auto& item : m_deviceObservers) {
        item->postGoHome();
    }
}

void Device::postGoMenu()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostGoMenu();

    for (const auto& item : m_deviceObservers) {
        item->postGoMenu();
    }
}

void Device::postAppSwitch()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostAppSwitch();

    for (const auto& item : m_deviceObservers) {
        item->postAppSwitch();
    }
}

void Device::postPower()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostPower();

    for (const auto& item : m_deviceObservers) {
        item->postPower();
    }
}

void Device::postVolumeUp()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostVolumeUp();

    for (const auto& item : m_deviceObservers) {
        item->postVolumeUp();
    }
}

void Device::postVolumeDown()
{
    if (!m_controller) {
        return;
    }
    m_controller->PostVolumeDown();

    for (const auto& item : m_deviceObservers) {
        item->postVolumeDown();
    }
}

void Device::postCopy()
{
    if (!m_controller) {
        return;
    }
    m_controller->Copy();

    for (const auto& item : m_deviceObservers) {
        item->postCopy();
    }
}

void Device::postCut()
{
    if (!m_controller) {
        return;
    }
    m_controller->Cut();

    for (const auto& item : m_deviceObservers) {
        item->postCut();
    }
}

void Device::setDisplayPower(bool on)
{
    if (!m_controller) {
        return;
    }
    m_controller->SetDisplayPower(on);

    for (const auto& item : m_deviceObservers) {
        item->setDisplayPower(on);
    }
}

void Device::expandNotificationPanel()
{
    if (!m_controller) {
        return;
    }
    m_controller->ExpandNotificationPanel();

    for (const auto& item : m_deviceObservers) {
        item->expandNotificationPanel();
    }
}

void Device::collapsePanel()
{
    if (!m_controller) {
        return;
    }
    m_controller->CollapsePanel();

    for (const auto& item : m_deviceObservers) {
        item->collapsePanel();
    }
}

void Device::postBackOrScreenOn(bool down)
{
    if (!m_controller) {
        return;
    }
    m_controller->PostBackOrScreenOn(down);

    for (const auto& item : m_deviceObservers) {
        item->postBackOrScreenOn(down);
    }
}

void Device::postTextInput(QString &text)
{
    if (!m_controller) {
        return;
    }
    m_controller->PostTextInput(text);

    for (const auto& item : m_deviceObservers) {
        item->postTextInput(text);
    }
}

void Device::requestDeviceClipboard()
{
    if (!m_controller) {
        return;
    }
    m_controller->RequestDeviceClipboard();

    for (const auto& item : m_deviceObservers) {
        item->requestDeviceClipboard();
    }
}

void Device::setDeviceClipboard(bool pause)
{
    if (!m_controller) {
        return;
    }
    m_controller->SetDeviceClipboard(pause);

    for (const auto& item : m_deviceObservers) {
        item->setDeviceClipboard(pause);
    }
}

void Device::clipboardPaste()
{
    if (!m_controller) {
        return;
    }
    m_controller->ClipboardPaste();

    for (const auto& item : m_deviceObservers) {
        item->clipboardPaste();
    }
}

void Device::pushFileRequest(const QString &file, const QString &devicePath)
{
    if (!m_fileHandler) {
        return;
    }
    m_fileHandler->onPushFileRequest(getSerial(), file, devicePath);

    for (const auto& item : m_deviceObservers) {
        item->pushFileRequest(file, devicePath);
    }
}

void Device::installApkRequest(const QString &apkFile)
{
    if (!m_fileHandler) {
        return;
    }
    m_fileHandler->onInstallApkRequest(getSerial(), apkFile);

    for (const auto& item : m_deviceObservers) {
        item->installApkRequest(apkFile);
    }
}

void Device::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!m_controller) {
        return;
    }
    m_controller->MouseEvent(from, frameSize, showSize);

    for (const auto& item : m_deviceObservers) {
        item->mouseEvent(from, frameSize, showSize);
    }
}

void Device::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!m_controller) {
        return;
    }
    m_controller->WheelEvent(from, frameSize, showSize);

    for (const auto& item : m_deviceObservers) {
        item->wheelEvent(from, frameSize, showSize);
    }
}

void Device::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!m_controller) {
        return;
    }
    m_controller->KeyEvent(from, frameSize, showSize);

    for (const auto& item : m_deviceObservers) {
        item->keyEvent(from, frameSize, showSize);
    }
}

bool Device::isCurrentCustomKeymap()
{
    if (!m_controller) {
        return false;
    }
    return m_controller->IsCurrentCustomKeymap();
}

bool Device::saveFrame(int width, int height, uint8_t* dataRGB32)
{
    if (!dataRGB32) {
        return false;
    }

    QImage rgbImage(dataRGB32, width, height, QImage::Format_RGB32);

    // save
    QString absFilePath;
    QString fileDir(m_params.recordPath);
    if (fileDir.isEmpty()) {
        qWarning() << "please select record save path!!!";
        return false;
    }
    QDateTime dateTime = QDateTime::currentDateTime();
    QString fileName = dateTime.toString("_yyyyMMdd_hhmmss_zzz");
    fileName = m_params.serial + fileName;
    fileName.replace(":", "_");
    fileName.replace(".", "_");
    fileName += ".png";
    QDir dir(fileDir);
    absFilePath = dir.absoluteFilePath(fileName);
    int ret = rgbImage.save(absFilePath, "PNG", 100);
    if (!ret) {
        return false;
    }

    qInfo() << "screenshot save to " << absFilePath;
    return true;
}

}
