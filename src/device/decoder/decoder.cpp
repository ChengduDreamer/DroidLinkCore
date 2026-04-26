#include <QDebug>

#include "avframeconvert.h"
#include "compat.h"
#include "decoder.h"
#include "fpscounter.h"
#include "videobuffer.h"

extern "C" {
#include "libavutil/imgutils.h"
}

Decoder::Decoder(FrameCallback onFrame, QObject *parent)
    : QObject(parent), m_vb(new VideoBuffer(this)),
      m_fpsCounter(new FpsCounter(this)), m_onFrame(std::move(onFrame)) {
    m_vb->Init();
    connect(this, &Decoder::NewFrame, this, &Decoder::OnNewFrame,
            Qt::QueuedConnection);
    connect(m_fpsCounter, &FpsCounter::updateFPS, this,
            &Decoder::UpdateFps);
}

Decoder::~Decoder() {
    m_vb->DeInit();
}

bool Decoder::Open() {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qCritical("H.264 decoder not found");
        return false;
    }
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical("Could not allocate decoder context");
        return false;
    }
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qCritical("Could not open H.264 codec");
        avcodec_free_context(&m_codecCtx);
        return false;
    }
    m_codecOpen = true;
    m_fpsCounter->start();
    return true;
}

void Decoder::Close() {
    if (m_vb) m_vb->Interrupt();
    if (m_fpsCounter) m_fpsCounter->stop();
    if (m_codecCtx) {
        if (m_codecOpen) avcodec_close(m_codecCtx);
        avcodec_free_context(&m_codecCtx);
        m_codecOpen = false;
    }
}

bool Decoder::Push(const AVPacket *packet) {
    if (!m_codecCtx || !m_vb) return false;

    AVFrame *decFrame = m_vb->DecodingFrame();
#ifdef QTSCRCPY_LAVF_HAS_NEW_ENCODING_DECODING_API
    int ret = avcodec_send_packet(m_codecCtx, packet);
    if (ret < 0) {
        char err[256] = {};
        av_strerror(ret, err, sizeof(err));
        qCritical("avcodec_send_packet: %s", err);
        return false;
    }
    if (decFrame) ret = avcodec_receive_frame(m_codecCtx, decFrame);
    if (!ret) {
        PushFrame();
    } else if (ret != AVERROR(EAGAIN)) {
        qCritical("avcodec_receive_frame: %d", ret);
        return false;
    }
#else
    int got = 0;
    int len = decFrame ? avcodec_decode_video2(m_codecCtx, decFrame, &got,
                                                packet) : -1;
    if (len < 0) {
        qCritical("avcodec_decode_video2: %d", len);
        return false;
    }
    if (got) PushFrame();
#endif
    return true;
}

void Decoder::PushFrame() {
    if (!m_vb) return;

    bool skipped = true;
    m_vb->OfferDecodedFrame(skipped);
    if (skipped) {
        m_fpsCounter->addSkippedFrame();
    }
    emit NewFrame();
}

void Decoder::OnNewFrame() {
    if (!m_onFrame || !m_vb) return;

    m_vb->Lock();
    const AVFrame *frame = m_vb->ConsumeRenderedFrame();
    m_fpsCounter->addRenderedFrame();
    m_onFrame(frame->width, frame->height, frame->data[0], frame->data[1],
              frame->data[2], frame->linesize[0], frame->linesize[1],
              frame->linesize[2]);
    m_vb->Unlock();
}

void Decoder::PeekFrame(
    std::function<void(int, int, uint8_t *)> onFrame) {
    if (!m_vb || !onFrame) return;

    m_vb->Lock();
    const AVFrame *frame = m_vb->ConsumeRenderedFrame();
    if (!frame) {
        m_vb->Unlock();
        return;
    }

    int w = frame->width, h = frame->height, ls = frame->linesize[0];
    auto *rgb = new uint8_t[ls * h * 4];
    AVFrame *rgbFrame = av_frame_alloc();
    if (!rgbFrame) {
        delete[] rgb;
        m_vb->Unlock();
        return;
    }

    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgb,
                          AV_PIX_FMT_RGB32, w, h, 4);

    AVFrameConvert convert;
    convert.setSrcFrameInfo(w, h, AV_PIX_FMT_YUV420P);
    convert.setDstFrameInfo(w, h, AV_PIX_FMT_RGB32);
    bool ok = convert.init() && convert.convert(frame, rgbFrame);
    convert.deInit();
    av_free(rgbFrame);
    m_vb->Unlock();

    if (ok) onFrame(w, h, rgb);
    delete[] rgb;
}
