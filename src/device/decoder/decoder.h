#ifndef DECODER_H
#define DECODER_H

#include <QObject>
#include <functional>

extern "C" {
#include "libavcodec/avcodec.h"
}

class FpsCounter;
class VideoBuffer;

class Decoder : public QObject {
    Q_OBJECT
public:
    using FrameCallback = std::function<void(
        int width, int height, uint8_t *dataY, uint8_t *dataU,
        uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV)>;

    Decoder(FrameCallback onFrame, QObject *parent = nullptr);
    ~Decoder() override;

    bool Open();
    void Close();
    bool Push(const AVPacket *packet);
    void PeekFrame(
        std::function<void(int, int, uint8_t *)> onFrame);

signals:
    void UpdateFps(quint32 fps);

private slots:
    void OnNewFrame();

signals:
    void NewFrame();

private:
    void PushFrame();

    VideoBuffer *m_vb = nullptr;
    FpsCounter *m_fpsCounter = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    bool m_codecOpen = false;
    FrameCallback m_onFrame;
};

#endif  // DECODER_H
