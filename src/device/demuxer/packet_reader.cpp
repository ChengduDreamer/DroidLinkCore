#include "packet_reader.h"
#include "videosocket.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

#define HEADER_SIZE 12
#define SC_PACKET_FLAG_CONFIG    (UINT64_C(1) << 63)
#define SC_PACKET_FLAG_KEY_FRAME (UINT64_C(1) << 62)
#define SC_PACKET_PTS_MASK       (SC_PACKET_FLAG_KEY_FRAME - 1)

static quint32 bufferRead32be(const quint8 *buf) {
    return (static_cast<quint32>(buf[0]) << 24) |
           (static_cast<quint32>(buf[1]) << 16) |
           (static_cast<quint32>(buf[2]) << 8) |
           static_cast<quint32>(buf[3]);
}

static quint64 bufferRead64be(const quint8 *buf) {
    quint32 msb = bufferRead32be(buf);
    quint32 lsb = bufferRead32be(&buf[4]);
    return (static_cast<quint64>(msb) << 32) | lsb;
}

PacketReader::PacketReader(VideoSocket *socket) : m_videoSocket(socket) {}

void PacketReader::SetVideoSocket(VideoSocket *socket) {
    m_videoSocket = socket;
}

qint32 PacketReader::RecvData(quint8 *buf, qint32 size) {
    if (!m_videoSocket) return 0;
    return m_videoSocket->subThreadRecvData(buf, size);
}

bool PacketReader::ReadPacket(AVPacket *packet) {
    quint8 header[HEADER_SIZE];
    qint32 r = RecvData(header, HEADER_SIZE);
    if (r < HEADER_SIZE) return false;

    quint64 ptsFlags = bufferRead64be(header);
    quint32 len = bufferRead32be(&header[8]);
    Q_ASSERT(len);

    if (av_new_packet(packet, static_cast<int>(len))) {
        return false;
    }

    r = RecvData(packet->data, static_cast<qint32>(len));
    if (r < 0 || static_cast<quint32>(r) < len) {
        av_packet_unref(packet);
        return false;
    }

    if (ptsFlags & SC_PACKET_FLAG_CONFIG) {
        packet->pts = AV_NOPTS_VALUE;
    } else {
        packet->pts = ptsFlags & SC_PACKET_PTS_MASK;
    }
    if (ptsFlags & SC_PACKET_FLAG_KEY_FRAME) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    packet->dts = packet->pts;
    return true;
}
