#include "theme.h"
#include "settings.h"
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QProxyStyle>
#include <QStyleHints>

namespace {

QString chosenTheme;

class PlainStyle : public QProxyStyle
{
public:
    PlainStyle() : QProxyStyle("Fusion") {}

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *data) const override
    {
        return hint == SH_UnderlineShortcut ? 0 : QProxyStyle::styleHint(hint, option, widget, data);
    }
};

const QHash<QString, QString> DarkColors = {
    {"text", "#ffffff"},
    {"muted", "#9d9d9d"},
    {"button", "#3c3c3c"},
    {"border", "#4a4a4a"},
    {"hover", "#464646"},
    {"hover-border", "#5a5a5a"},
    {"pressed", "#333333"},
    {"disabled", "#2a2a2a"},
    {"disabled-text", "#6e6e6e"},
    {"disabled-border", "#333333"},
    {"field", "#2d2d2d"},
    {"alternate", "#262626"},
    {"separator", "#3c3c3c"},
    {"scroll", "#4a4a4a"},
    {"scroll-hover", "#5a5a5a"},
    {"check-border", "#5a5a5a"},
    {"plus", ":/plus.svg"},
    {"arrow", ":/arrow-down.svg"},
};

const QHash<QString, QString> LightColors = {
    {"text", "#1b1b1b"},
    {"muted", "#666666"},
    {"button", "#f3f3f3"},
    {"border", "#d4d4d4"},
    {"hover", "#e9e9e9"},
    {"hover-border", "#bdbdbd"},
    {"pressed", "#e0e0e0"},
    {"disabled", "#f6f6f6"},
    {"disabled-text", "#a3a3a3"},
    {"disabled-border", "#e3e3e3"},
    {"field", "#ffffff"},
    {"alternate", "#f7f7f7"},
    {"separator", "#e3e3e3"},
    {"scroll", "#c6c6c6"},
    {"scroll-hover", "#a6a6a6"},
    {"check-border", "#a6a6a6"},
    {"plus", ":/plus-light.svg"},
    {"arrow", ":/arrow-down-light.svg"},
};

QString styleSheet() {
    QFile file(":/launcher.qss");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString style = QString::fromUtf8(file.readAll());
    const QHash<QString, QString> &colors = Theme::dark() ? DarkColors : LightColors;
    for (auto it = colors.begin(); it != colors.end(); ++it)
        style.replace("@" + it.key() + "@", it.value());
    return style;
}

}

void Theme::apply(QApplication &app) {
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app]() {
        app.setStyleSheet(styleSheet());
    });
    use(Settings::theme());
}

void Theme::use(const QString &theme) {
    chosenTheme = theme;
    QGuiApplication::styleHints()->setColorScheme(theme == "dark" ? Qt::ColorScheme::Dark
        : theme == "light" ? Qt::ColorScheme::Light : Qt::ColorScheme::Unknown);
    qApp->setStyleSheet(styleSheet());
}

QStyle *Theme::style() {
    return new PlainStyle;
}

QIcon Theme::icon(const QString &path) {
    QPixmap pixmap(path);
    if (!dark()) {
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), QColor(0x3c, 0x3c, 0x3c));
    }
    return QIcon(pixmap);
}

bool Theme::dark() {
    if (chosenTheme == "dark") return true;
    if (chosenTheme == "light") return false;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
