#include "videobuffer.h"

extern "C" {
#include "libavutil/frame.h"
}

VideoBuffer::VideoBuffer(QObject *parent) : QObject(parent) {}

VideoBuffer::~VideoBuffer() {}

bool VideoBuffer::Init() {
    m_decodingFrame = av_frame_alloc();
    if (!m_decodingFrame) goto error;
    m_renderingFrame = av_frame_alloc();
    if (!m_renderingFrame) goto error;
    m_renderingFrameConsumed = true;
    return true;

error:
    DeInit();
    return false;
}

void VideoBuffer::DeInit() {
    if (m_decodingFrame) {
        av_frame_free(&m_decodingFrame);
    }
    if (m_renderingFrame) {
        av_frame_free(&m_renderingFrame);
    }
}

void VideoBuffer::Lock() { m_mutex.lock(); }

void VideoBuffer::Unlock() { m_mutex.unlock(); }

void VideoBuffer::SetRenderExpiredFrames(bool value) {
    m_renderExpiredFrames = value;
}

AVFrame *VideoBuffer::DecodingFrame() { return m_decodingFrame; }

void VideoBuffer::OfferDecodedFrame(bool &previousFrameSkipped) {
    m_mutex.lock();

    if (m_renderExpiredFrames) {
        while (!m_renderingFrameConsumed && !m_interrupted) {
            m_renderingFrameConsumedCond.wait(&m_mutex);
        }
    }

    Swap();
    previousFrameSkipped = !m_renderingFrameConsumed;
    m_renderingFrameConsumed = false;
    m_mutex.unlock();
}

const AVFrame *VideoBuffer::ConsumeRenderedFrame() {
    Q_ASSERT(!m_renderingFrameConsumed);
    m_renderingFrameConsumed = true;
    if (m_renderExpiredFrames) {
        m_renderingFrameConsumedCond.wakeOne();
    }
    return m_renderingFrame;
}

void VideoBuffer::Interrupt() {
    if (m_renderExpiredFrames) {
        m_mutex.lock();
        m_interrupted = true;
        m_mutex.unlock();
        m_renderingFrameConsumedCond.wakeOne();
    }
}

void VideoBuffer::Swap() {
    AVFrame *tmp = m_decodingFrame;
    m_decodingFrame = m_renderingFrame;
    m_renderingFrame = tmp;
}
