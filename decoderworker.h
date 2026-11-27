#ifndef DECODERWORKER_H
#define DECODERWORKER_H

#include <QObject>
#include <QImage>
#include <QString>

#include "pausedecoder.h"
#include "mediastruct.h"
#include "safequeue.h"

class VideoDecoder;
class AudioDecoder;
class AudioClock;
class MediaClock;
class Barrier;

using PacketQueue = SafeQueue<AVPacket*>;

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

    std::optional<InfoVideo> open(const QString &filePath);
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

    std::atomic<bool> seekRequested_{};
    std::atomic<double> seekTarget_{};

    std::shared_ptr<MediaClock> mediaClock_;
    std::shared_ptr<Barrier> barrier_;

    void finish();
};

#endif // DECODERWORKER_H
