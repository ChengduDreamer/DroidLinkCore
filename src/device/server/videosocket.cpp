#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include "videosocket.h"

VideoSocket::VideoSocket(QObject *parent) : QTcpSocket(parent) {}

VideoSocket::~VideoSocket() {}

qint32 VideoSocket::subThreadRecvData(quint8 *buf, qint32 bufSize,
                                       int timeoutMs) {
    if (!buf) {
        return 0;
    }
    Q_ASSERT(QCoreApplication::instance()->thread() !=
             QThread::currentThread());

    if (timeoutMs > 0) {
        qint64 totalRead = 0;
        while (totalRead < bufSize) {
            qint64 avail = bytesAvailable();
            if (avail > 0) {
                qint64 toRead = qMin(avail, static_cast<qint64>(bufSize - totalRead));
                qint64 n = read(reinterpret_cast<char *>(buf) + totalRead, toRead);
                if (n <= 0) return 0;
                totalRead += n;
            } else {
                if (!waitForReadyRead(timeoutMs)) {
                    return 0;
                }
            }
        }
        return static_cast<qint32>(totalRead);
    }

    // Legacy: block forever until bufSize bytes available
    while (bytesAvailable() < bufSize) {
        if (!waitForReadyRead(-1)) {
            return 0;
        }
    }
    return read(reinterpret_cast<char *>(buf), bufSize);
}
