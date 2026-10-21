#include "serverconfigdialog.h"
#include "paths.h"
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr auto Reference = "https://github.com/openkogama/openkogama-server#launcher-servers";
const QStringList Fixed = {"httpPort", "photonPort", "publicUrl", "listenHost"};
const QString Starter = R"({
    "title": "",
    "joinMode": "play",
    "accounts": "optional",
    "clients": [],
    "mixedClients": false,
    "keepPlayChanges": false,
    "public": false,
    "playerSettings": true,
    "playerAvatars": true,
    "playerInventory": true
}
)";
const QList<QPair<QString, bool>> Defaults = {
    {"public", false}, {"playerSettings", true}, {"playerAvatars", true}, {"playerInventory", true}};

QString configPath()
{
    return QDir(Paths::serverData()).filePath("server.json");
}

QString withKey(const QString &text, const QString &name, const QString &value)
{
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject() || document.object().contains(name)) return text;
    const qsizetype end = text.lastIndexOf('}');
    qsizetype last = end - 1;
    while (last > 0 && text[last].isSpace()) --last;
    QString indent(4, ' ');
    if (const qsizetype key = text.indexOf('"'); key > 0) {
        const qsizetype line = text.lastIndexOf('\n', key) + 1;
        const QString before = text.mid(line, key - line);
        if (line > 0 && before.trimmed().isEmpty()) indent = before;
    }
    const QString comma = text[last] == '{' ? QString() : QString(",");
    return text.left(last + 1) + comma + "\n" + indent + "\"" + name + "\": " + value + "\n" + text.mid(end);
}

QString withDefaults(QString text)
{
    for (const auto &[name, value] : Defaults)
        text = withKey(text, name, value ? "true" : "false");
    return withKey(text, "accounts", "\"optional\"");
}

QString jsonList(const QStringList &values)
{
    QStringList quoted;
    for (const QString &value : values)
        quoted << "\"" + value + "\"";
    return "[" + quoted.join(", ") + "]";
}
}

QStringList ServerConfigDialog::admins()
{
    QStringList list;
    for (const QJsonValue &value : current().value("admins").toArray())
        if (value.isString()) list << value.toString();
    return list;
}

bool ServerConfigDialog::setAdmins(const QStringList &admins)
{
    QFile file(configPath());
    QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : Starter;
    file.close();
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject()) return false;
    static const QRegularExpression listed(R"("admins"\s*:\s*\[[^\]]*\])");
    text = document.object().contains("admins") ? text.replace(listed, "\"admins\": " + jsonList(admins)) : withKey(text, "admins", jsonList(admins));
    if (QJsonDocument::fromJson(text.toUtf8()).object().value("admins").toArray() != QJsonArray::fromStringList(admins)) return false;
    QDir().mkpath(Paths::serverData());
    QSaveFile saved(configPath());
    return saved.open(QIODevice::WriteOnly) && saved.write(text.toUtf8()) >= 0 && saved.commit();
}

QJsonObject ServerConfigDialog::current()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

ServerConfigDialog::ServerConfigDialog(QWidget *parent)
    : QDialog(parent), m_text(new QPlainTextEdit(this)), m_error(new QLabel(this))
{
    setWindowTitle("Server settings");
    resize(520, 420);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *hint = new QLabel(QString("The server.json of the server on this computer, which hosted worlds follow. "
        "Saving restarts the server. <a href=\"%1\" style=\"color: #3a7a80; text-decoration: none;\">All settings</a>").arg(QLatin1String(Reference)), this);
    hint->setWordWrap(true);
    hint->setOpenExternalLinks(true);
    layout->addWidget(hint);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(font().pointSizeF());
    m_text->setObjectName("serverConfigEdit");
    m_text->setFont(mono);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFile file(configPath());
    m_text->setPlainText(file.open(QIODevice::ReadOnly) ? withDefaults(QString::fromUtf8(file.readAll())) : Starter);
    layout->addWidget(m_text, 1);
    m_error->setObjectName("serverConfigError");
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto *folder = buttons->addButton("Open folder", QDialogButtonBox::ResetRole);
    auto *confirm = buttons->button(QDialogButtonBox::Save);
    confirm->setProperty("primary", true);
    confirm->style()->polish(confirm);
    connect(folder, &QPushButton::clicked, this, []() {
        QDir().mkpath(Paths::serverData());
        QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::serverData()));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &ServerConfigDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ServerConfigDialog::save()
{
    QJsonParseError error;
    QByteArray text = m_text->toPlainText().toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);
    QString problem;
    if (error.error != QJsonParseError::NoError)
        problem = "Line " + QString::number(text.left(error.offset).count('\n') + 1) + ": " + error.errorString();
    else if (!document.isObject())
        problem = "The settings have to be one { } object.";
    for (const QString &key : Fixed)
        if (problem.isEmpty() && document.object().contains(key))
            problem = "Leave out " + key + ", the launcher picks the ports and address of this server itself.";
    if (!problem.isEmpty()) {
        m_error->setText(problem);
        m_error->show();
        return;
    }
    if (!text.endsWith('\n')) text += '\n';
    QDir().mkpath(Paths::serverData());
    QSaveFile file(configPath());
    if (!file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit()) {
        m_error->setText("Could not save " + QDir::toNativeSeparators(configPath()));
        m_error->show();
        return;
    }
    accept();
}
