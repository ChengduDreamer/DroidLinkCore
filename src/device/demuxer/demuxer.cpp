#include <QDebug>

#include "compat.h"
#include "demuxer.h"
#include "packet_reader.h"
#include "videosocket.h"

extern "C" {
#include "libavformat/avformat.h"
}

static void avLogCallback(void *, int level, const char *fmt, va_list vl) {
    QString msg = QString::fromUtf8(fmt).trimmed();
    msg.prepend("[FFmpeg] ");
    switch (level) {
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:    qFatal("%s", qPrintable(msg)); break;
    case AV_LOG_ERROR:    qCritical() << msg;            break;
    case AV_LOG_WARNING:  qWarning()  << msg;            break;
    case AV_LOG_INFO:     qInfo()     << msg;            break;
    default:                                              break;
    }
}

Demuxer::Demuxer(QObject *parent) : QThread(parent),
    m_reader(new PacketReader()) {}

Demuxer::~Demuxer() { delete m_reader; }

bool Demuxer::Init() {
#ifdef QTSCRCPY_LAVF_REQUIRES_REGISTER_ALL
    av_register_all();
#endif
    if (avformat_network_init()) return false;
    av_log_set_callback(avLogCallback);
    return true;
}

void Demuxer::DeInit() { avformat_network_deinit(); }

void Demuxer::InstallVideoSocket(VideoSocket *socket) {
    socket->moveToThread(this);
    m_videoSocket = socket;
    m_reader->SetVideoSocket(socket);
}

void Demuxer::SetFrameSize(const QSize &size) { m_frameSize = size; }

bool Demuxer::StartDecode() {
    if (!m_videoSocket) return false;
    start();
    return true;
}

void Demuxer::StopDecode() { wait(); }

void Demuxer::run() {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qCritical("H.264 decoder not found");
        goto runQuit;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical("Could not allocate codec context");
        goto runQuit;
    }
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->width = m_frameSize.width();
    m_codecCtx->height = m_frameSize.height();
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    m_parser = av_parser_init(AV_CODEC_ID_H264);
    if (!m_parser) {
        qCritical("Could not initialize parser");
        goto runQuit;
    }
    m_parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;

    {
        AVPacket *packet = av_packet_alloc();
        if (!packet) {
            qCritical("OOM");
            goto runQuit;
        }

        while (m_reader->ReadPacket(packet)) {
            if (!PushPacket(packet)) break;
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
    }

    if (m_pending) av_packet_free(&m_pending);

    av_parser_close(m_parser);

runQuit:
    if (m_codecCtx) avcodec_free_context(&m_codecCtx);

    if (m_videoSocket) {
        m_videoSocket->close();
        delete m_videoSocket;
        m_videoSocket = nullptr;
    }

    emit OnStreamStop();
}

bool Demuxer::PushPacket(AVPacket *packet) {
    bool isConfig = packet->pts == AV_NOPTS_VALUE;

    if (m_pending || isConfig) {
        qint32 offset;
        if (m_pending) {
            offset = m_pending->size;
            if (av_grow_packet(m_pending, packet->size)) return false;
        } else {
            offset = 0;
            m_pending = av_packet_alloc();
            if (av_new_packet(m_pending, packet->size)) {
                av_packet_free(&m_pending);
                return false;
            }
        }
        memcpy(m_pending->data + offset, packet->data,
               static_cast<size_t>(packet->size));
        if (!isConfig) {
            m_pending->pts = packet->pts;
            m_pending->dts = packet->dts;
            m_pending->flags = packet->flags;
            packet = m_pending;
        }
    }

    if (isConfig) {
        return ProcessConfigPacket(packet);
    }

    bool ok = ParseAndEmit(packet);
    if (m_pending) av_packet_free(&m_pending);
    return ok;
}

bool Demuxer::ProcessConfigPacket(AVPacket *packet) {
    emit GetConfigFrame(packet);
    return true;
}

bool Demuxer::ParseAndEmit(AVPacket *packet) {
    quint8 *outData = nullptr;
    int outLen = 0;
    int r = av_parser_parse2(m_parser, m_codecCtx, &outData, &outLen,
                              packet->data, packet->size, AV_NOPTS_VALUE,
                              AV_NOPTS_VALUE, -1);
    Q_ASSERT(r == packet->size);
    Q_ASSERT(outLen == packet->size);
    (void)r;
    (void)outLen;

    if (m_parser->key_frame == 1) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    packet->dts = packet->pts;
    emit GetFrame(packet);
    return true;
}
