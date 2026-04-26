#ifndef VIDEOSOCKET_H
#define VIDEOSOCKET_H

#include <QTcpSocket>

class VideoSocket : public QTcpSocket {
    Q_OBJECT
public:
    explicit VideoSocket(QObject *parent = nullptr);
    ~VideoSocket() override;

    // Blocking read for worker thread use. Returns bytes read, or 0 on
    // error/timeout. If timeoutMs > 0, returns after timeout if data
    // is incomplete. timeoutMs == 0 blocks forever (legacy).
    qint32 subThreadRecvData(quint8 *buf, qint32 bufSize,
                              int timeoutMs = 0);
};

#endif  // VIDEOSOCKET_H
