#include "theme.h"
#include "settings.h"
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>
#include <optional>

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

QString resourceText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString styleSheet() {
    QString style = resourceText(":/launcher.qss");
#ifndef Q_OS_WIN
    style += resourceText(":/launcher-linux.qss");
#endif
    const QHash<QString, QString> &colors = Theme::dark() ? DarkColors : LightColors;
    for (auto it = colors.begin(); it != colors.end(); ++it)
        style.replace("@" + it.key() + "@", it.value());
    return style;
}

#ifndef Q_OS_WIN
std::optional<bool> desktopDark;

bool desktopPrefersDark() {
    if (qEnvironmentVariable("GTK_THEME").contains("dark", Qt::CaseInsensitive))
        return true;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const QString &gtk : {QStringLiteral("gtk-4.0"), QStringLiteral("gtk-3.0")}) {
        QFile file(config + "/" + gtk + "/settings.ini");
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        for (const QString &line : QString::fromUtf8(file.readAll()).split('\n')) {
            const QString key = line.section('=', 0, 0).trimmed();
            const QString value = line.section('=', 1).trimmed();
            if (key == "gtk-application-prefer-dark-theme" && (value == "1" || value.compare("true", Qt::CaseInsensitive) == 0))
                return true;
            if (key == "gtk-theme-name" && value.contains("dark", Qt::CaseInsensitive))
                return true;
        }
    }
    return false;
}

QPalette darkPalette() {
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

QPalette lightPalette() {
    QStyle *fusion = QStyleFactory::create("Fusion");
    const QPalette palette = fusion ? fusion->standardPalette() : QPalette();
    delete fusion;
    return palette;
}
#endif

void refresh() {
#ifndef Q_OS_WIN
    desktopDark.reset();
    qApp->setPalette(Theme::dark() ? darkPalette() : lightPalette());
#endif
    qApp->setStyleSheet(styleSheet());
}

}

void Theme::apply(QApplication &app) {
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, refresh);
    use(Settings::theme());
}

void Theme::use(const QString &theme) {
    chosenTheme = theme;
    QGuiApplication::styleHints()->setColorScheme(theme == "dark" ? Qt::ColorScheme::Dark
        : theme == "light" ? Qt::ColorScheme::Light : Qt::ColorScheme::Unknown);
    refresh();
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
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
#ifndef Q_OS_WIN
    if (scheme == Qt::ColorScheme::Unknown) {
        if (!desktopDark) desktopDark = desktopPrefersDark();
        return *desktopDark;
    }
#endif
    return scheme == Qt::ColorScheme::Dark;
}
