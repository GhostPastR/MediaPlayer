#include "decoderworker.h"
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QTimer>

#include "packetqueue.h"
#include "videodecoder.h"
#include "audiodecoder.h"
#include "mediaqueue.h"
#include "playerthread.h"
#include "mediaclock.h"

DecoderWorker::DecoderWorker(QObject *parent) : QObject(parent) {
    videoQueue_ = std::make_shared<PacketQueue>();
    audioQueue_ = std::make_shared<PacketQueue>();
    mediaQueue_ = std::make_shared<MediaQueue>();
}

DecoderWorker::~DecoderWorker() {
    stop();
}

std::optional<DecoderWorker::InfoVideo> DecoderWorker::open(const QString &filePath)
{
    QByteArray pathBytes = filePath.toLocal8Bit();
    int ret = avformat_open_input(&pFormatCtx_, pathBytes.constData(), nullptr, nullptr);
    if (ret < 0) {
        qDebug() << tr("Не удалось открыть файл");
        return {};
    }
    // Получаем информацию о потоках
    if (avformat_find_stream_info(pFormatCtx_, nullptr) < 0) {
        emit error("Не удалось получить информацию о потоках");
        finish();
        return {};
    }
    DecoderWorker::InfoVideo info;
    info.format = pFormatCtx_->iformat->name;
    info.duration_sec = pFormatCtx_->duration / AV_TIME_BASE;
    info.count_stream = pFormatCtx_->nb_streams;

    for (unsigned int i = 0; i < pFormatCtx_->nb_streams; i++) {
        AVStream *stream = pFormatCtx_->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;
        if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            info.videoCodec = avcodec_get_name(codecpar->codec_id);
            info.width = codecpar->width;
            info.height = codecpar->height;
        }
        else if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            info.audioCodec = avcodec_get_name(codecpar->codec_id);
            info.channel = codecpar->channel_layout;
            info.sample = codecpar->sample_rate;
        }
    }
    return info;
}

std::optional<CodecFormat> DecoderWorker::createDecoders()
{
    videoIndex_ = av_find_best_stream(pFormatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioIndex_ = av_find_best_stream(pFormatCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    mediaClock_ = std::make_shared<MediaClock>();

    videoDecoder_ = new VideoDecoder(pFormatCtx_, videoIndex_, videoQueue_, mediaQueue_, this);
    audioDecoder_ = new AudioDecoder(pFormatCtx_, audioQueue_, mediaClock_, this);
    playerThread_ = new PlayerThread(mediaQueue_, mediaClock_, this);
    connect(playerThread_, &PlayerThread::frameReady, this,
            &DecoderWorker::frameReady, Qt::DirectConnection);

    return audioDecoder_->createContext(audioIndex_);
}

int DecoderWorker::getSecond() const
{
    if(mediaClock_){
        return mediaClock_->getReferenceTime();
    }
    return 0;
}

void DecoderWorker::pause()
{
    qDebug() << "DecoderWorker - Pause";
    if(playerThread_){
        playerThread_->pausePlayback();
    }
    if(audioDecoder_){
        audioDecoder_->pause();
    }
    if(mediaClock_){
        mediaClock_->pause();
    }
}

void DecoderWorker::resume()
{
    if(playerThread_){
        playerThread_->resumePlayback();
    }
    if(audioDecoder_){
        audioDecoder_->resume();
    }
    if(mediaClock_){
        mediaClock_->resume();
    }
}

void DecoderWorker::close()
{
    if(pFormatCtx_){
        avformat_close_input(&pFormatCtx_);
    }
}

float DecoderWorker::volume() const
{
    if(audioDecoder_){
        return audioDecoder_->volume();
    }
    return {};
}

void DecoderWorker::setVolume(float volume)
{
    if(audioDecoder_){
        audioDecoder_->setVolume(volume);
    }
}

void DecoderWorker::setSpeed(double speed, double seconds)
{
    qDebug() << "[DecoderWorker::setSpeed]" << speed;
    pausePlayback();
    if(videoDecoder_){
        videoDecoder_->pausePlayback();
    }
    if(audioDecoder_){
        audioDecoder_->pausePlayback();
    }
    if(playerThread_){
        playerThread_->pausePlayback();
    }
    videoQueue_->clear();
    audioQueue_->clear();
    mediaQueue_->clear();

    if(audioDecoder_){
        audioDecoder_->setSpeed(speed);
    }

    resumePlayback();
    if(videoDecoder_){
        videoDecoder_->resumePlayback();
    }
    if(audioDecoder_){
        audioDecoder_->resumePlayback();
    }
    if(playerThread_){
        playerThread_->resumePlayback();
    }
}

void DecoderWorker::stop()
{
    stopPlayback();

    if(videoDecoder_){
        videoDecoder_->stop();
        videoDecoder_->wait();
    }
    delete videoDecoder_;
    videoDecoder_ = nullptr;

    if(audioDecoder_){
        audioDecoder_->stop();
        audioDecoder_->wait();
    }
    delete audioDecoder_;
    audioDecoder_ = nullptr;

    if(playerThread_){
        playerThread_->stop();
        playerThread_->wait();
    }
    delete playerThread_;
    playerThread_ = nullptr;

    close();
}

void DecoderWorker::seek(double seconds)
{
    seekTarget_ = seconds;
    seekRequested_ = true;
    if(videoDecoder_){
        videoDecoder_->seekFlush();
    }
    if(audioDecoder_){
        audioDecoder_->seekFlush(seconds);
    }
    if(playerThread_){
        playerThread_->seekFlush(seconds);
    }
}

void DecoderWorker::finish()
{
    emit finished();
    close();
}

void DecoderWorker::start()
{
    stopRequested_ = false;
    if(!videoDecoder_){
        qDebug() << "Error videoDecoder_ DecoderWorker::start";
        return;
    }
    if(!audioDecoder_){
        qDebug() << "Error audioDecoder_ DecoderWorker::start";
        return;
    }

    videoDecoder_->start();
    audioDecoder_->start();
    playerThread_->start();

    AVPacket *packet = av_packet_alloc();
    while (!stopRequested_) {
        waitPaused();
        if (seekRequested_) {
            double targetSec = seekTarget_.load();
            qDebug() << "targetSecb = " << targetSec;

            // Перематываем все потоки к целевой позиции
            int64_t targetUs = static_cast<int64_t>(targetSec * AV_TIME_BASE);
            int ret = av_seek_frame(pFormatCtx_, -1, targetUs, AVSEEK_FLAG_BACKWARD);
//            AVStream *videoStream = pFormatCtx_->streams[videoIndex_];
//            int64_t targetPts = static_cast<int64_t>(targetSec / av_q2d(videoStream->time_base));

//            int ret = avformat_seek_file(pFormatCtx_, videoIndex_, targetPts - 1,
//                                         targetPts, targetPts+1, AVSEEK_FLAG_BACKWARD);
            if (ret < 0) {
                qDebug() << "av_seek_frame failed:" << ret;
            }

            // Очищаем очереди от старых пакетов
            videoQueue_->clear();
            audioQueue_->clear();
            mediaQueue_->clear();
            seekRequested_ = false;
            continue;
        }

        int ret = av_read_frame(pFormatCtx_, packet);
        if (ret < 0) {
            // Конец файла — маркеры конца
            videoQueue_->push(nullptr);
            audioQueue_->push(nullptr);
            continue;
        }

        if (packet->stream_index == videoIndex_) {
            videoQueue_->push(packet);
        } else if (packet->stream_index == audioIndex_) {
            audioQueue_->push(packet);
        } else {
            av_packet_unref(packet);
        }

        packet = av_packet_alloc();
    }

    av_packet_free(&packet);
}
