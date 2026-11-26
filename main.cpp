#include "mainwindow.h"
#include "detoverlay.h"
#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 跨线程信号槽的参数类型注册（解决 not compatible 报错）
    qRegisterMetaType<DetItem>("DetItem");
    qRegisterMetaType<QVector<DetItem>>("QVector<DetItem>");

    MainWindow w;
    w.show();
    return app.exec();
}
