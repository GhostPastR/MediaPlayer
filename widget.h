#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class VideoWidget;
class DecoderWorker;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

signals:
    void seekRequested(double seconds);

private slots:
    void on_pushButton_clicked();
    void on_pbPlay_clicked();
    void onPositionChanged(int seconds);
    void on_sliderTime_sliderMoved(int position);
    void on_cbSpeedVideo_currentIndexChanged(int index);

    void on_audioVolume_sliderMoved(int position);

private:
    Ui::Widget *ui;

    bool isPaused_{true};
    int durationVideo_{};
    double speedVideo_ = 1;

    QThread *decodeThread_{};
    DecoderWorker *decoder_{};
    VideoWidget *videoWidget_{};

    void openVideo();
    void closeVideo();
    int valueSlider(int value);
};
#endif // WIDGET_H
