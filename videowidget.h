#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <mutex>

#include <QWidget>
#include <QImage>
#include <QTimer>

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);

public slots:
    // Устанавливает новый кадр и планирует перерисовку
    void setFrame(std::shared_ptr<QImage> frame);

    // Очищает виджет (например, при остановке)
    void clear();

private slots:
    void onNewFrame();
    void onResizeFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    std::shared_ptr<QImage> frontBuffer_;   // читает paintEvent
    std::shared_ptr<QImage> backBuffer_;    // пишет DecoderWorker
    std::mutex mutex_;
    std::atomic<bool> updateScheduled_{false};
    std::atomic<bool> resizing_{false};
    QTimer m_resizeTimer;
};

#endif // VIDEOWIDGET_H
