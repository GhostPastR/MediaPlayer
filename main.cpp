#include "widget.h"
#include <QApplication>

//#include "barrier.h"
//#include "thread"

//#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Widget w;
    w.show();
    return a.exec();
}

//int main() {
//    constexpr std::size_t N = 4;
//    Barrier barrier(N);

//    std::vector<std::thread> workers;
//    for (std::size_t i = 0; i < N; ++i) {
//        workers.emplace_back([&]{
//            for (int r = 0; r < 3; ++r) {
//                // работа
//                qDebug() << "r" << r;
//                barrier.arrive_and_wait();  // +1; блок только если main ждёт
//                // продолжение
//                qDebug() << "re" << r;
//            }
//        });
//    }

//    for (int r = 0; r < 3; ++r) {
//        qDebug() << "1";
//        barrier.wait_all_arrived();      // включить и дождаться всех
//        qDebug() << "11";
//        // работа main, пока заблокированные стоят
//        barrier.release();      // отпустить и перейти к раунду r+1
//        qDebug() << "111";
//    }

//    for (auto& t : workers) t.join();
//}
