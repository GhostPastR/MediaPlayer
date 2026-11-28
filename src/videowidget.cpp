#include "videowidget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QDebug>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    // Чёрный фон, чтобы не было артефактов до первого кадра
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    QTimer *resizeTimer = new QTimer(this);
    connect(resizeTimer, &QTimer::timeout, this, &VideoWidget::onResizeFinished);
    resizeTimer->start(100);
}

void VideoWidget::setFrame(std::shared_ptr<QImage> frame)
{
    {
        std::lock_guard locker(mutex_);
        backBuffer_ = std::move(frame); // только присваивание указателя, без копии пикселей
    }

    // Планируем обработку в UI-потоке максимум один раз
    if (!updateScheduled_.exchange(true)) {
        QMetaObject::invokeMethod(this, "onNewFrame", Qt::QueuedConnection);
    }
}

void VideoWidget::clear()
{
    {
        std::lock_guard locker(mutex_);
        frontBuffer_.reset();
        backBuffer_.reset();
    }
    update();
}

void VideoWidget::onNewFrame()
{
    {
        std::lock_guard locker(mutex_);
        std::swap(frontBuffer_, backBuffer_);
    }
    updateScheduled_ = false;
    // Во время ресайза не перерисовываем каждый кадр
    if (resizing_.load()) {
        return;
    }
    update();
}

void VideoWidget::onResizeFinished()
{
    resizing_ = false;
    update(); // перерисовываем актуальный кадр в новом размере
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    std::shared_ptr<QImage> frame;
    {
        std::lock_guard locker(mutex_);
        frame = frontBuffer_; // копируем указатель под мьютексом, быстро
    }

    QPainter painter(this);
    if (!frame || frame->isNull()) {
        painter.fillRect(rect(), Qt::black);
        return;
    }

    // Масштабируем с сохранением пропорций
    QSize scaled = frame->size().scaled(size(), Qt::KeepAspectRatio);
    QRect target((width() - scaled.width()) / 2,
                 (height() - scaled.height()) / 2,
                 scaled.width(), scaled.height());

    painter.fillRect(rect(), Qt::black);
    painter.drawImage(target, *frame);
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    resizing_ = true;
    QWidget::resizeEvent(event);
}
