#ifndef PACKET_READER_H
#define PACKET_READER_H

#include <QObject>

class VideoSocket;
struct AVPacket;

// Reads the scrcpy 12-byte header + NAL data protocol from VideoSocket.
// PTS flags are decoded inline; the caller receives a raw AVPacket.
class PacketReader {
public:
    explicit PacketReader(VideoSocket *socket = nullptr);

    void SetVideoSocket(VideoSocket *socket);

    // Reads one complete packet. Returns true on success.
    bool ReadPacket(AVPacket *packet);

private:
    qint32 RecvData(quint8 *buf, qint32 size);

    VideoSocket *m_videoSocket = nullptr;
};

#endif  // PACKET_READER_H
