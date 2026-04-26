#include <QApplication>
#include <QClipboard>
#include <QDebug>

#include "controller.h"
#include "controlmsg.h"
#include "devicemsg.h"
#include "inputconvertgame.h"

Controller::Controller(SendFunc sendData, QString gameScript,
                         QObject *parent)
    : QObject(parent), m_sendData(std::move(sendData)) {
    // Default clipboard provider: direct QApplication access
    m_clipboardGetter = []() -> QString {
        return QApplication::clipboard()->text();
    };
    m_clipboardSetter = [](const QString &text) {
        QApplication::clipboard()->setText(text);
    };
    UpdateScript(gameScript);
}

Controller::~Controller() {}

void Controller::SetClipboardProvider(ClipboardGetFunc getter,
                                       ClipboardSetFunc setter) {
    if (getter) m_clipboardGetter = std::move(getter);
    if (setter) m_clipboardSetter = std::move(setter);
}

void Controller::PostControlMsg(ControlMsg *controlMsg) {
    if (controlMsg) {
        QCoreApplication::postEvent(this, controlMsg);
    }
}

// ---------------------------------------------------------------------------
// Device → PC message handling (was in Receiver)
// ---------------------------------------------------------------------------

void Controller::RecvDeviceMsg(DeviceMsg *deviceMsg) {
    if (!deviceMsg) return;

    switch (deviceMsg->type()) {
    case DeviceMsg::DMT_GET_CLIPBOARD: {
        QString text;
        deviceMsg->getClipboardMsgData(text);
        if (m_clipboardGetter) {
            if (m_clipboardGetter() == text) {
                qDebug("Computer clipboard unchanged");
                break;
            }
        }
        if (m_clipboardSetter) m_clipboardSetter(text);
        qInfo("Device clipboard copied");
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Script & keymap
// ---------------------------------------------------------------------------

void Controller::UpdateScript(QString gameScript) {
    if (m_inputConvert) delete m_inputConvert;

    if (!gameScript.isEmpty()) {
        auto *game = new InputConvertGame(this);
        game->loadKeyMap(gameScript);
        m_inputConvert = game;
    } else {
        m_inputConvert = new InputConvertNormal(this);
    }
    connect(m_inputConvert, &InputConvertBase::grabCursor, this,
            &Controller::GrabCursor);
}

bool Controller::IsCurrentCustomKeymap() {
    return m_inputConvert && m_inputConvert->isCurrentCustomKeymap();
}

// ---------------------------------------------------------------------------
// Device actions
// ---------------------------------------------------------------------------

void Controller::PostGoBack()          { PostKeyCodeClick(AKEYCODE_BACK); }
void Controller::PostGoHome()          { PostKeyCodeClick(AKEYCODE_HOME); }
void Controller::PostGoMenu()          { PostKeyCodeClick(AKEYCODE_MENU); }
void Controller::PostAppSwitch()       { PostKeyCodeClick(AKEYCODE_APP_SWITCH); }
void Controller::PostPower()           { PostKeyCodeClick(AKEYCODE_POWER); }
void Controller::PostVolumeUp()        { PostKeyCodeClick(AKEYCODE_VOLUME_UP); }
void Controller::PostVolumeDown()      { PostKeyCodeClick(AKEYCODE_VOLUME_DOWN); }
void Controller::Copy()                { PostKeyCodeClick(AKEYCODE_COPY); }
void Controller::Cut()                 { PostKeyCodeClick(AKEYCODE_CUT); }

void Controller::PostBackOrScreenOn(bool down) {
    auto *msg = new ControlMsg(ControlMsg::CMT_BACK_OR_SCREEN_ON);
    msg->setBackOrScreenOnData(down);
    PostControlMsg(msg);
}

void Controller::ExpandNotificationPanel() {
    auto *msg = new ControlMsg(ControlMsg::CMT_EXPAND_NOTIFICATION_PANEL);
    PostControlMsg(msg);
}

void Controller::CollapsePanel() {
    auto *msg = new ControlMsg(ControlMsg::CMT_COLLAPSE_PANELS);
    PostControlMsg(msg);
}

void Controller::SetDisplayPower(bool on) {
    auto *msg = new ControlMsg(ControlMsg::CMT_SET_DISPLAY_POWER);
    msg->setDisplayPowerData(on);
    PostControlMsg(msg);
}

void Controller::RequestDeviceClipboard() {
    auto *msg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    PostControlMsg(msg);
}

void Controller::GetDeviceClipboard(bool cut) {
    auto *msg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    ControlMsg::GetClipboardCopyKey key =
        cut ? ControlMsg::GCCK_CUT : ControlMsg::GCCK_COPY;
    msg->setGetClipboardMsgData(key);
    PostControlMsg(msg);
}

void Controller::SetDeviceClipboard(bool pause) {
    auto *msg = new ControlMsg(ControlMsg::CMT_SET_CLIPBOARD);
    QString text = m_clipboardGetter();
    msg->setSetClipboardMsgData(text, pause);
    PostControlMsg(msg);
}

void Controller::ClipboardPaste() {
    QString text = m_clipboardGetter();
    PostTextInput(text);
}

void Controller::PostTextInput(QString &text) {
    auto *msg = new ControlMsg(ControlMsg::CMT_INJECT_TEXT);
    msg->setInjectTextMsgData(text);
    PostControlMsg(msg);
}

// ---------------------------------------------------------------------------
// Input events → input converter
// ---------------------------------------------------------------------------

void Controller::MouseEvent(const QMouseEvent *from, const QSize &frameSize,
                              const QSize &showSize) {
    if (m_inputConvert) m_inputConvert->mouseEvent(from, frameSize, showSize);
}
void Controller::WheelEvent(const QWheelEvent *from, const QSize &frameSize,
                              const QSize &showSize) {
    if (m_inputConvert) m_inputConvert->wheelEvent(from, frameSize, showSize);
}
void Controller::KeyEvent(const QKeyEvent *from, const QSize &frameSize,
                            const QSize &showSize) {
    if (m_inputConvert) m_inputConvert->keyEvent(from, frameSize, showSize);
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

bool Controller::event(QEvent *event) {
    if (event &&
        static_cast<ControlMsg::Type>(event->type()) == ControlMsg::Control) {
        auto *msg = dynamic_cast<ControlMsg *>(event);
        if (msg) SendControlMsg(msg->serializeData());
        return true;
    }
    return QObject::event(event);
}

bool Controller::SendControlMsg(const QByteArray &buffer) {
    if (buffer.isEmpty() || !m_sendData) return false;
    return static_cast<qint32>(m_sendData(buffer)) == buffer.length();
}

void Controller::PostKeyCodeClick(AndroidKeycode keycode) {
    auto *down = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    down->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, keycode, 0,
                                   AMETA_NONE);
    PostControlMsg(down);

    auto *up = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    up->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_UP, keycode, 0, AMETA_NONE);
    PostControlMsg(up);
}
