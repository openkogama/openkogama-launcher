#include "mainwindow.h"

#include <QApplication>
#include <QStyleHints>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("Fusion");
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    a.setOrganizationName("OpenKogama");
    a.setApplicationName("OpenKogama Launcher");
    MainWindow w;
    w.show();
    return QApplication::exec();
}