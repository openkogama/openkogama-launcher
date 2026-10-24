#ifndef THEME_H
#define THEME_H

class QApplication;
class QIcon;
class QString;
class QStyle;

namespace Theme {

void apply(QApplication &app);
void use(const QString &theme);
bool dark();
QIcon icon(const QString &path);
QStyle *style();

}

#endif // THEME_H
