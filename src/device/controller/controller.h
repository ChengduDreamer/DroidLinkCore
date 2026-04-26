#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <functional>

#include "inputconvertbase.h"

class DeviceMsg;
class InputConvertBase;

class Controller : public QObject {
    Q_OBJECT
public:
    using SendFunc = std::function<qint64(const QByteArray &)>;
    using ClipboardGetFunc = std::function<QString()>;
    using ClipboardSetFunc = std::function<void(const QString &)>;

    Controller(SendFunc sendData, QString gameScript = "",
               QObject *parent = nullptr);
    ~Controller() override;

    void SetClipboardProvider(ClipboardGetFunc getter,
                               ClipboardSetFunc setter);

    void PostControlMsg(ControlMsg *controlMsg);
    void RecvDeviceMsg(DeviceMsg *deviceMsg);

    void UpdateScript(QString gameScript = "");
    bool IsCurrentCustomKeymap();

    // Device actions
    void PostGoBack();
    void PostGoHome();
    void PostGoMenu();
    void PostAppSwitch();
    void PostPower();
    void PostVolumeUp();
    void PostVolumeDown();
    void Copy();
    void Cut();
    void ExpandNotificationPanel();
    void CollapsePanel();
    void SetDisplayPower(bool on);
    void PostBackOrScreenOn(bool down);
    void RequestDeviceClipboard();
    void GetDeviceClipboard(bool cut = false);
    void SetDeviceClipboard(bool pause = true);
    void ClipboardPaste();
    void PostTextInput(QString &text);

    // Input events
    void MouseEvent(const QMouseEvent *from, const QSize &frameSize,
                    const QSize &showSize);
    void WheelEvent(const QWheelEvent *from, const QSize &frameSize,
                    const QSize &showSize);
    void KeyEvent(const QKeyEvent *from, const QSize &frameSize,
                  const QSize &showSize);

signals:
    void GrabCursor(bool grab);

protected:
    bool event(QEvent *event) override;

private:
    bool SendControlMsg(const QByteArray &buffer);
    void PostKeyCodeClick(AndroidKeycode keycode);

    QPointer<InputConvertBase> m_inputConvert;
    SendFunc m_sendData;
    ClipboardGetFunc m_clipboardGetter;
    ClipboardSetFunc m_clipboardSetter;
};

#endif  // CONTROLLER_H
