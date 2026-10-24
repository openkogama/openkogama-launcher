#include "languages.h"
#include "links.h"
#include "mainwindow.h"
#include "theme.h"
#include "updater.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    Updater::startup();
    QApplication a(argc, argv);
    const QString link = Links::find(a.arguments());
    if (!link.isEmpty() && Links::forward(link)) return 0;
    a.setStyle(Theme::style());
    a.setOrganizationName("OpenKogama");
    a.setApplicationName("OpenKogama Launcher");
    Theme::apply(a);
    Languages::install(a);
    Links::registerScheme();
    MainWindow w;
    Links::listen(&w, [&w](const QString &opened) { w.openLink(opened); });
    w.show();
    Updater::check(&w);
    if (!link.isEmpty()) QTimer::singleShot(0, &w, [&w, link]() { w.openLink(link); });
    return QApplication::exec();
}
