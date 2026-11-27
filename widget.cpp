#include "widget.h"
#include "./ui_widget.h"

#include <QThread>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>

#include "videowidget.h"
#include "decoderworker.h"

#include <QDebug>

constexpr int timeIntervalMSec = 100;
constexpr int maxAudioVolume = 100;
const QString mediaFile = QObject::tr("Медиафайлы (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.mp3 *.wav *.flac *.aac *.ogg *.m4a);;"
                             "Видео (*.mp4 *.avi *.mkv *.mov *.wmv *.flv);;"
                             "Аудио (*.mp3 *.wav *.flac *.aac *.ogg *.m4a);;"
                             "Все файлы (*.*)");
const QString textTitle = QObject::tr("Открыть медиафайл");

QString timeConvert(int seconds){
    return QString("%1:%2.%3")
            .arg(seconds / 3600, 2, 10, QChar('0'))
            .arg((seconds % 3600) / 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0'));
}

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    QVBoxLayout *layout = new QVBoxLayout;
    videoWidget_ = new VideoWidget();
    layout->addWidget(videoWidget_);
    ui->wVideo->setLayout(layout);

    connect(ui->sliderTime, &SeekSlider::clickedAt, this, [this](int value) {
        emit seekRequested(valueSlider(value));
    });
}

Widget::~Widget()
{
    closeVideo();
    delete ui;
}

void Widget::on_pushButton_clicked()
{
    if(openVideo()){
        isPaused_ = false;

        // Запускаем поток
        decodeThread_->start();
    }
}

void Widget::on_pbPlay_clicked()
{
    ui->pbPlay->setText(isPaused_ ? tr("Pause") : tr("Play"));
    if (isPaused_) {
        qDebug() << "Resume";
        decoder_->resume();
    } else {
        qDebug() << "Pause";
        decoder_->pause();
    }
    isPaused_ = !isPaused_;
}

void Widget::onPositionChanged(int seconds)
{
    if(durationVideo_ < seconds){
        ui->sliderTime->setValue(valueSlider(durationVideo_));
    }
    else{
        ui->lbCurrent->setText(timeConvert(seconds));
        if (durationVideo_ > 0) {
            ui->sliderTime->setValue(valueSlider(seconds));
        }
    }
}

void Widget::on_sliderTime_sliderMoved(int position)
{
    ui->lbCurrent->setText(timeConvert(valueSlider(position)));
}

void Widget::on_cbSpeedVideo_currentIndexChanged(int index)
{  
    speedVideo_ = (index + 1) * 0.5;
    if(decoder_){
        double time = valueSlider(ui->sliderTime->value());
        decoder_->setSpeed(speedVideo_, time);
    }
}

bool Widget::openVideo()
{
    QString fileName = QFileDialog::getOpenFileName(this, textTitle,
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), mediaFile);
    if(fileName.isEmpty()){
        qDebug() << "Файл не выбран";
        return false;
    }
    closeVideo();

    decodeThread_ = new QThread(this);
    decoder_ = new DecoderWorker();

//    const QString fileName = "C:/1.avi";
//    const QString fileName = "C:/5.avi";
//    const QString fileName = "C:/3.mp3";
    auto info = decoder_->open(fileName);
    if(!info.has_value()){
        qDebug() << "Error openVideo";
        return false;
    }
    auto codec = decoder_->createDecoders();
    decoder_->moveToThread(decodeThread_);

    if(!codec.has_value()){
        qDebug() << "Error createDecoders";
        return false;
    }
    durationVideo_ = info->duration_sec;
    ui->lbDuration->setText(timeConvert(durationVideo_));
    qDebug() << "Format = " << QString::fromStdString(info->format);
    qDebug() << "Duration = " << info->duration_sec;
    qDebug() << "Count = " << info->count_stream;

    auto max = durationVideo_;
    ui->sliderTime->setMaximum(max);
    ui->sliderTime->setSingleStep(durationVideo_ / max);

    // Соединяем сигналы
    connect(decodeThread_, &QThread::started, decoder_, [this]() {
        decoder_->start(); // путь к файлу
    });
    connect(decoder_, &DecoderWorker::frameReady, videoWidget_,
            &VideoWidget::setFrame, Qt::DirectConnection);
    connect(decoder_, &DecoderWorker::finished, decodeThread_, &QThread::quit);
    connect(decoder_, &DecoderWorker::error, this, [](const QString &msg) {
        qDebug() << "Ошибка декодера:" << msg;
    });
    connect(decoder_, &DecoderWorker::positionChanged, this, &Widget::onPositionChanged);
    connect(this, &Widget::seekRequested, decoder_, &DecoderWorker::seek, Qt::DirectConnection);

    decoder_->setSpeed(speedVideo_, -1);

    qDebug() << "Audio volume " << decoder_->volume();
    ui->audioVolume->setValue(maxAudioVolume);

    QTimer *timer = new QTimer(this);
    timer->setInterval(timeIntervalMSec);
    connect(timer, &QTimer::timeout, this, [this]() {
        if(decoder_){
            onPositionChanged(decoder_->getSecond());
        }
    });
    timer->start();
    return true;
}

void Widget::closeVideo()
{
    if (decoder_) {
        decoder_->stop();  // просим декодер завершить работу
    }
    if (decodeThread_) {
        decodeThread_->quit();
        decodeThread_->wait();  // ЖДЁМ завершения потока
    }
    // Поток умер, decoder_ больше не в своём потоке. Удаляем безопасно.
    delete decoder_;
    delete decodeThread_;
}

int Widget::valueSlider(int value)
{
    return (value * durationVideo_) / ui->sliderTime->maximum();
}

void Widget::on_audioVolume_sliderMoved(int position)
{
    if(decoder_){
        auto volume = static_cast<float>(position) / ui->audioVolume->maximum();
        decoder_->setVolume(volume);
    }
}
