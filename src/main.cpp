#include <QApplication>
#include "mainwindow.h"
#include <QByteArray>
int main(int argc,char *argv[])
{
    qputenv("QT_GSTREAMER_PLAYBIN_AUDIOSINK",
        QByteArrayLiteral("alsasink"));

    QApplication app(argc, argv);
    MainWindow window;
    window.showFullScreen();

    return app.exec();
}