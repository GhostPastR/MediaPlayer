#ifndef SEEKSLIDER_H
#define SEEKSLIDER_H

#include <QSlider>
#include <QMouseEvent>
#include <QStyle>

class SeekSlider : public QSlider
{
    Q_OBJECT
public:
    explicit SeekSlider(QWidget *parent = nullptr) : QSlider(parent) {}

signals:
    void clickedAt(int value);

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // SEEKSLIDER_H
