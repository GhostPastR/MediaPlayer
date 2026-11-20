#ifndef DECODERWORKER_H
#define DECODERWORKER_H

#include <QObject>
#include <QImage>
#include <QString>

#include "miniaudio.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "pausedecoder.h"
#include "mediastruct.h"

class PacketQueue;
class VideoDecoder;
class AudioDecoder;
class AudioClock;
class MediaQueue;
class PlayerThread;
class MediaClock;

class DecoderWorker : public QObject, public PauseStopDecoder
{
    Q_OBJECT

public:
    struct InfoVideo
    {
        std::string format;
        int duration_sec{};
        int count_stream{};

        std::string videoCodec;
        int width;
        int height;

        std::string audioCodec;
        uint64_t channel;
        int sample;
    };

    explicit DecoderWorker(QObject *parent = nullptr);
    ~DecoderWorker();

    InfoVideo open(const QString &filePath);
    std::optional<CodecFormat> createDecoders();
    int getSecond() const;

    void pause();
    void resume();
    void close();

    float volume() const;
    void setVolume(float volume);

    void setSpeed(double speed, double seconds);

public slots:
    void start(); // запуск декодирования
    void stop();  // запрос на остановку

    void seek(double seconds);

signals:
    void frameReady(std::shared_ptr<QImage>);
    void finished();                      // декодирование завершено
    void error(const QString &message);   // ошибка
    void positionChanged(int seconds);

private:
    int videoIndex_{-1};
    int audioIndex_{-1};

    AVFormatContext *pFormatCtx_{};
    std::shared_ptr<PacketQueue> videoQueue_;
    VideoDecoder* videoDecoder_{};

    std::shared_ptr<PacketQueue> audioQueue_;
    AudioDecoder* audioDecoder_{};

    std::shared_ptr<MediaQueue> mediaQueue_;
    PlayerThread *playerThread_{};

    std::atomic<bool> seekRequested_{};
    std::atomic<double> seekTarget_{};

    std::shared_ptr<MediaClock> mediaClock_;

    void finish();
};

#endif // DECODERWORKER_H
