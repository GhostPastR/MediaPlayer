#include "seekslider.h"

void SeekSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Вычисляем позицию по координате клика
        int value = QStyle::sliderValueFromPosition(minimum(), maximum(), event->pos().x(), width());
        setValue(value);
        emit clickedAt(value);
        event->accept();
    }
    QSlider::mousePressEvent(event);
}
