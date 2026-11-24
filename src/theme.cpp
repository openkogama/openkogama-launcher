#include "theme.h"
#include "settings.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QStyleFactory>
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
    {"window", "#222222"},
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
    {"window", "#f7f7f7"},
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

bool isSystemDark() {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) return true;
    if (scheme == Qt::ColorScheme::Light) return false;

#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    const QString gtkEnv = qEnvironmentVariable("GTK_THEME");
    if (!gtkEnv.isEmpty() && gtkEnv.contains("dark", Qt::CaseInsensitive))
        return true;

    for (const char *path : {"~/.config/gtk-3.0/settings.ini", "~/.config/gtk-4.0/settings.ini"}) {
        QString fullPath = QString::fromLatin1(path);
        if (fullPath.startsWith('~')) fullPath.replace(0, 1, QDir::homePath());
        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QString content = QString::fromUtf8(file.readAll());
            if (content.contains("gtk-application-prefer-dark-theme=1", Qt::CaseInsensitive))
                return true;
            if (content.contains("gtk-theme-name=") && content.contains("dark", Qt::CaseInsensitive))
                return true;
        }
    }
#endif

    return false;
}

QPalette createDarkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#222222"));
    palette.setColor(QPalette::WindowText, QColor("#ffffff"));
    palette.setColor(QPalette::Base, QColor("#2d2d2d"));
    palette.setColor(QPalette::AlternateBase, QColor("#262626"));
    palette.setColor(QPalette::ToolTipBase, QColor("#2d2d2d"));
    palette.setColor(QPalette::ToolTipText, QColor("#ffffff"));
    palette.setColor(QPalette::Text, QColor("#ffffff"));
    palette.setColor(QPalette::Button, QColor("#3c3c3c"));
    palette.setColor(QPalette::ButtonText, QColor("#ffffff"));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor("#3a7a80"));
    palette.setColor(QPalette::Highlight, QColor("#3a7a80"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#9d9d9d"));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#6e6e6e"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#6e6e6e"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6e6e6e"));
    palette.setColor(QPalette::Disabled, QPalette::Button, QColor("#2a2a2a"));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor("#222222"));
    palette.setColor(QPalette::Disabled, QPalette::Window, QColor("#222222"));
    return palette;
}

QPalette createLightPalette() {
    QStyle *fusion = QStyleFactory::create("Fusion");
    QPalette palette = fusion ? fusion->standardPalette() : QPalette();
    delete fusion;
    return palette;
}

}

void Theme::apply(QApplication &app) {
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, []() {
        Theme::use(Settings::theme());
    });
    use(Settings::theme());
}

void Theme::use(const QString &theme) {
    chosenTheme = theme;
    const bool darkTheme = Theme::dark();
    QGuiApplication::styleHints()->setColorScheme(darkTheme ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    if (qApp) {
        qApp->setPalette(darkTheme ? createDarkPalette() : createLightPalette());
        qApp->setStyleSheet(styleSheet());
    }
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
    return isSystemDark();
}
