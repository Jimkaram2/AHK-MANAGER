#include "mainwindow.h"
#include <QApplication>
#include <qIcon>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/appicon.ico"));
    MainWindow w;
    w.show();
    return a.exec();
}
