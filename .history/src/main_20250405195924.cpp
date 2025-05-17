#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <iostream>
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
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