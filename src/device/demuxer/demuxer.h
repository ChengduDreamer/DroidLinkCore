#ifndef DEMUXER_H
#define DEMUXER_H

#include <QPointer>
#include <QSize>
#include <QThread>

extern "C" {
#include "libavcodec/avcodec.h"
}

class PacketReader;
class VideoSocket;

class Demuxer : public QThread {
    Q_OBJECT
public:
    explicit Demuxer(QObject *parent = nullptr);
    ~Demuxer() override;

    static bool Init();
    static void DeInit();

    void InstallVideoSocket(VideoSocket *socket);
    void SetFrameSize(const QSize &size);
    bool StartDecode();
    void StopDecode();

signals:
    void OnStreamStop();
    void GetFrame(AVPacket *packet);
    void GetConfigFrame(AVPacket *packet);

protected:
    void run() override;

private:
    bool ProcessConfigPacket(AVPacket *packet);
    bool ParseAndEmit(AVPacket *packet);
    bool PushPacket(AVPacket *packet);

    PacketReader *m_reader = nullptr;
    QPointer<VideoSocket> m_videoSocket;
    QSize m_frameSize;
    AVPacket *m_pending = nullptr;
    AVCodecParserContext *m_parser = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
};

#endif  // DEMUXER_H
