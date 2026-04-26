#ifndef VIDEO_BUFFER_H
#define VIDEO_BUFFER_H

#include <QMutex>
#include <QWaitCondition>
#include <QObject>

typedef struct AVFrame AVFrame;

// Pure double-buffered AVFrame manager with mutex protection.
// Decoding thread offers frames; rendering thread consumes them.
class VideoBuffer : public QObject {
    Q_OBJECT
public:
    explicit VideoBuffer(QObject *parent = nullptr);
    ~VideoBuffer() override;

    bool Init();
    void DeInit();

    void Lock();
    void Unlock();

    // Returns the frame that the decoder can write into.
    AVFrame *DecodingFrame();

    // Called from decoder thread to hand off a freshly decoded frame.
    // previousFrameSkipped: true if the previous frame was not consumed.
    void OfferDecodedFrame(bool &previousFrameSkipped);

    // Called from rendering thread (WITH Lock held) to get the frame.
    const AVFrame *ConsumeRenderedFrame();

    // Wake up any blocking wait in OfferDecodedFrame.
    void Interrupt();

    void SetRenderExpiredFrames(bool value);

private:
    void Swap();

    AVFrame *m_decodingFrame = nullptr;
    AVFrame *m_renderingFrame = nullptr;
    QMutex m_mutex;
    bool m_renderingFrameConsumed = true;
    bool m_renderExpiredFrames = false;
    bool m_interrupted = false;
    QWaitCondition m_renderingFrameConsumedCond;
};

#endif  // VIDEO_BUFFER_H
