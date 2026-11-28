#include "decoderworker.h"
#include <QDebug>

#include "videodecoder.h"
#include "audiodecoder.h"
#include "mediaclock.h"
#include "barrier.h"

constexpr std::size_t countThread = 3;

DecoderWorker::DecoderWorker(QObject *parent) : QObject(parent) {
    videoQueue_ = std::make_shared<PacketQueue>(50);
    audioQueue_ = std::make_shared<PacketQueue>(50);
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
    barrier_ = std::make_shared<Barrier>(3);

    videoDecoder_ = new VideoDecoder(pFormatCtx_, videoIndex_, videoQueue_, mediaClock_, barrier_, this);
    connect(videoDecoder_, &VideoDecoder::frameReady, this,
            &DecoderWorker::frameReady, Qt::DirectConnection);
    audioDecoder_ = new AudioDecoder(pFormatCtx_, audioQueue_, mediaClock_, barrier_, this);
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
    paused_ = true;
    qDebug() << "DecoderWorker - Pause";
    if(videoDecoder_){
        videoDecoder_->pause();
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
    paused_ = false;
    if(videoDecoder_){
        videoDecoder_->resume();
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
    if(videoDecoder_){
        videoDecoder_->stop();
    }
    if(audioDecoder_){
        audioDecoder_->stop();
    }
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
    if(audioDecoder_){
        audioDecoder_->setSpeed(speed);
    }
    if(seconds > 0){
        seekTarget_ = seconds;
        seekRequested_ = true;
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

    close();
}

void DecoderWorker::seek(double seconds)
{
    seekTarget_ = seconds;
    seekRequested_ = true;
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

    AVPacket *packet = av_packet_alloc();
    while (!stopRequested_) {
        if (seekRequested_.exchange(false)) {
            barrier_->wait_all_arrived();

            if(paused_){
                if(videoDecoder_){
                    videoDecoder_->resume();
                }
                if(audioDecoder_){
                    audioDecoder_->resume();
                }
                if(mediaClock_){
                    mediaClock_->resume();
                }
            }

            double targetSec = seekTarget_.load();
            if(videoDecoder_){
                videoDecoder_->seekFlush(targetSec);
            }
            if(audioDecoder_){
                audioDecoder_->seekFlush(targetSec);
            }

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

            if(paused_){
                if(videoDecoder_){
                    videoDecoder_->resume();
                }
                if(audioDecoder_){
                    audioDecoder_->resume();
                }
                if(mediaClock_){
                    mediaClock_->resume();
                }
            }
            barrier_->release();
            continue;
        }

        int ret = av_read_frame(pFormatCtx_, packet);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "av_read_frame error:" << ret << errbuf;

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
    qDebug() << "[DecoderWorker] завершён";
}
