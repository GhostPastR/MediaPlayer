#include "widget.h"
#include "./ui_widget.h"

#include <QDebug>
#include <QDir>
#include <QLabel>
#include <QThread>
#include <QAudioOutput>

#include "videowidget.h"
#include "decoderworker.h"
#include "playerthread.h"

constexpr int maxTimeSec = 1000;
//const QString file = "C:/1.avi";
//const QString file = "C:/2.avi";
const QString file = "C:/3.mp3";

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
    closeVideo();
    openVideo();
    isPaused_ = false;

    // Запускаем поток
    decodeThread_->start();
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
    speedVideo_ = index + 1;
    if(decoder_){
        double time = valueSlider(ui->sliderTime->value());
        decoder_->setSpeed(speedVideo_, time);
    }
}

void Widget::openVideo()
{
    decodeThread_ = new QThread(this);
    decoder_ = new DecoderWorker();
    decoder_->moveToThread(decodeThread_);

    auto info = decoder_->open(file);
    auto codec = decoder_->createDecoders();
    if(!codec.has_value()){
        qDebug() << "Error openVideo";
    }
    durationVideo_ = info.duration_sec;
    ui->lbDuration->setText(timeConvert(durationVideo_));
    qDebug() << "Format = " << QString::fromStdString(info.format);
    qDebug() << "Duration = " << info.duration_sec;
    qDebug() << "Count = " << info.count_stream;

    auto max = /*info.duration_sec > 1000 ? 1000 : */durationVideo_;
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
    decoder_->setSpeed(speedVideo_, -1.0);

    qDebug() << "Audio volume " << decoder_->volume();
    ui->audioVolume->setValue(100);

    QTimer *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this]() {
        if(decoder_){
            onPositionChanged(decoder_->getSecond());
        }
    });
    timer->start();
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
        auto volume = 2. * static_cast<float>(position) / ui->audioVolume->maximum();
        decoder_->setVolume(volume);
    }
}
