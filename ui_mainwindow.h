#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QPushButton *openFilesButton;
    QHBoxLayout *horizontalLayout;
    QLineEdit *search;
    QPushButton *searchButton;
    QListWidget *fileList;
    QGraphicsView *graph;
    QPushButton *toggleFunctionGraph;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(800, 600);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        openFilesButton = new QPushButton(centralwidget);
        openFilesButton->setObjectName(QString::fromUtf8("openFilesButton"));

        verticalLayout->addWidget(openFilesButton);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        search = new QLineEdit(centralwidget);
        search->setObjectName(QString::fromUtf8("search"));

        horizontalLayout->addWidget(search);

        searchButton = new QPushButton(centralwidget);
        searchButton->setObjectName(QString::fromUtf8("searchButton"));

        horizontalLayout->addWidget(searchButton);


        verticalLayout->addLayout(horizontalLayout);

        fileList = new QListWidget(centralwidget);
        fileList->setObjectName(QString::fromUtf8("fileList"));

        verticalLayout->addWidget(fileList);

        graph = new CustomGraphView(centralwidget);
        graph->setObjectName(QString::fromUtf8("graph"));

        verticalLayout->addWidget(graph);

        toggleFunctionGraph = new QPushButton(centralwidget);
        toggleFunctionGraph->setObjectName(QString::fromUtf8("toggleFunctionGraph"));

        verticalLayout->addWidget(toggleFunctionGraph);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName(QString::fromUtf8("menubar"));
        menubar->setGeometry(QRect(0, 0, 800, 22));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "CFG Parser", nullptr));
        openFilesButton->setText(QCoreApplication::translate("MainWindow", "Open Files", nullptr));
        search->setPlaceholderText(QCoreApplication::translate("MainWindow", "Search function...", nullptr));
        searchButton->setText(QCoreApplication::translate("MainWindow", "Search", nullptr));
        toggleFunctionGraph->setText(QCoreApplication::translate("MainWindow", "Toggle Function Graph", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
