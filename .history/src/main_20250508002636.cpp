#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <iostream>
#include <QtWebEngine/qtwebengineglobal.h>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QtWebEngine::initialize();
    
    QApplication app(argc, argv);
    qputenv("QT_DEBUG_PLUGINS", "1");
    
    try {
        MainWindow window;
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        qCritical() << "Fatal error:" << e.what();
        QMessageBox::critical(nullptr, "Fatal Error", 
                            QString("Application failed to initialize:\n%1").arg(e.what()));
        return 1;
    }
}