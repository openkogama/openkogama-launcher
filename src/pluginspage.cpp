#include "pluginspage.h"
#include "paths.h"
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtCore/private/qzipreader_p.h>

namespace {

constexpr int StatusTimeout = 3000;
constexpr int ReloadTimeout = 30000;
constexpr int FileRole = Qt::UserRole;
constexpr int OnRole = Qt::UserRole + 1;

QString place(bool on)
{
    const QString plugins = QDir(Paths::serverData()).filePath("plugins");
    return on ? plugins : QDir(plugins).filePath("disabled");
}

QString location(const QString &file, bool on)
{
    return QDir(place(on)).filePath(file);
}

bool plain(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    return !clean.isEmpty() && clean != "." && clean != ".." && !clean.startsWith("../")
        && !clean.startsWith('/') && !clean.contains(':');
}

bool write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool addDll(const QString &file, const QByteArray &data)
{
    if (data.isEmpty() || !plain(file)) return false;
    QFile::remove(location(file, false));
    return write(location(file, true), data);
}

QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString wrapper(const QStringList &names)
{
    QString top;
    for (const QString &name : names) {
        const qsizetype slash = name.indexOf('/');
        if (slash <= 0) return {};
        if (top.isEmpty()) top = name.left(slash);
        else if (name.left(slash) != top) return {};
    }
    return top.isEmpty() || top.compare("data", Qt::CaseInsensitive) == 0 ? QString() : top + '/';
}

int addZip(const QString &path)
{
    QZipReader zip(path);
    if (!zip.isReadable()) return 0;
    const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
    QStringList names;
    for (const QZipReader::FileInfo &entry : entries) names << QString(entry.filePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString top = wrapper(names);
    int added = 0;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (!entries[i].isFile || !names[i].startsWith(top)) continue;
        const QString name = names[i].mid(top.size());
        if (name.startsWith("data/", Qt::CaseInsensitive)) {
            const QString target = QDir(place(true)).filePath("data/" + QDir::cleanPath(name.mid(5)));
            if (plain(name.mid(5)) && !QFileInfo::exists(target)) write(target, zip.fileData(entries[i].filePath));
        } else if (name.endsWith(".dll", Qt::CaseInsensitive) && addDll(name.section('/', -1), zip.fileData(entries[i].filePath))) {
            ++added;
        }
    }
    return added;
}

QStringList pluginFiles(const QMimeData *mime)
{
    QStringList paths;
    for (const QUrl &url : mime->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(".dll", Qt::CaseInsensitive) || path.endsWith(".zip", Qt::CaseInsensitive)) paths << path;
    }
    return paths;
}

}

PluginsPage::PluginsPage(QWidget *parent)
    : QWidget(parent), m_net(new QNetworkAccessManager(this)), m_list(new QTreeWidget(this)),
      m_empty(new QLabel(this)), m_status(new QLabel(this))
{
    setObjectName("pluginsPage");
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    auto *warning = new QLabel(tr("Only add plugins you trust: they run on this computer like any other program."), this);
    warning->setWordWrap(true);
    auto *reload = new QPushButton(tr("Reload"), this);
    reload->setToolTip(tr("Load the plugins again on the server on this computer"));
    auto *open = new QPushButton(tr("Open folder"), this);
    auto *add = new QPushButton(tr("Add plugin..."), this);
    add->setObjectName("addPluginButton");
    toolbar->addWidget(warning, 1);
    toolbar->addWidget(reload);
    toolbar->addWidget(open);
    toolbar->addWidget(add);
    layout->addLayout(toolbar);

    m_empty->setWordWrap(true);
    m_empty->setTextFormat(Qt::PlainText);
    m_empty->setText(tr("No plugins yet. Drop a .dll or .zip file here or click Add plugin."));
    layout->addWidget(m_empty);

    m_list->setObjectName("pluginsList");
    m_list->setAccessibleName(tr("Plugins"));
    m_list->setHeaderLabels({tr("Plugin"), tr("Status")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setMinimumSectionSize(160);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setText(tr("The server on this computer is not running, so plugins load the next time it starts."));
    m_status->hide();
    layout->addWidget(m_status);

    connect(reload, &QPushButton::clicked, this, [this]() { ask(true); });
    connect(open, &QPushButton::clicked, this, []() {
        QDir().mkpath(place(true));
        QDesktopServices::openUrl(QUrl::fromLocalFile(place(true)));
    });
    connect(add, &QPushButton::clicked, this, &PluginsPage::addPlugins);
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) { toggle(item); });
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &PluginsPage::showMenu);
    auto *remove = new QShortcut(QKeySequence::Delete, m_list);
    remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, &PluginsPage::removePlugin);
}

void PluginsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
    ask(false);
}

void PluginsPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!pluginFiles(event->mimeData()).isEmpty()) event->acceptProposedAction();
}

void PluginsPage::dropEvent(QDropEvent *event)
{
    const QStringList paths = pluginFiles(event->mimeData());
    if (paths.isEmpty()) return;
    event->acceptProposedAction();
    QTimer::singleShot(0, this, [this, paths]() { install(paths); });
}

void PluginsPage::refresh()
{
    const QString selected = m_list->currentItem() ? m_list->currentItem()->data(0, FileRole).toString() : QString();
    m_filling = true;
    m_list->clear();
    for (const bool on : {true, false}) {
        for (const QFileInfo &file : QDir(place(on)).entryInfoList({"*.dll"}, QDir::Files)) {
            auto *item = new QTreeWidgetItem(m_list, {file.completeBaseName(), on ? tr("On") : tr("Off")});
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            item->setData(0, FileRole, file.fileName());
            item->setData(0, OnRole, on);
            if (file.fileName() == selected) m_list->setCurrentItem(item);
        }
    }
    m_list->sortItems(0, Qt::AscendingOrder);
    m_filling = false;
    m_empty->setVisible(m_list->topLevelItemCount() == 0);
}

void PluginsPage::ask(bool reload)
{
    QNetworkRequest request(QUrl(LocalServerUrl + (reload ? "/api/admin/plugins/reload" : "/api/admin/plugins")));
    request.setTransferTimeout(reload ? ReloadTimeout : StatusTimeout);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = reload ? m_net->post(request, QByteArray()) : m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const bool running = reply->error() == QNetworkReply::NoError && document.isArray();
        m_status->setVisible(!running);
        showStatus(document.array(), running);
    });
}

void PluginsPage::showStatus(const QJsonArray &files, bool running)
{
    QHash<QString, QJsonObject> loaded;
    for (const QJsonValue &value : files)
        loaded.insert(value.toObject().value("file").toString().toLower(), value.toObject());
    m_filling = true;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        QString status = tr("Off");
        QString tip;
        if (item->data(0, OnRole).toBool()) {
            const QJsonObject file = loaded.value(item->data(0, FileRole).toString().toLower());
            QStringList plugins;
            for (const QJsonValue &plugin : file.value("plugins").toArray()) plugins << plugin.toString();
            if (!running) {
                status = tr("On");
            } else if (file.isEmpty()) {
                status = tr("Not loaded yet");
                tip = tr("Click Reload to load it");
            } else if (!file.value("error").toString().isEmpty()) {
                status = tr("Failed to load");
                tip = file.value("error").toString();
            } else if (plugins.isEmpty()) {
                status = tr("Library");
                tip = tr("Has no plugin of its own, other plugins can use it");
            } else {
                status = tr("Running");
                tip = plugins.join('\n');
            }
        }
        item->setText(1, status);
        item->setToolTip(1, tip);
    }
    m_filling = false;
}

void PluginsPage::addPlugins()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add plugin"), QString(), tr("Plugins (*.dll *.zip)"));
    if (!paths.isEmpty()) install(paths);
}

void PluginsPage::install(const QStringList &paths)
{
    QStringList failed;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const bool added = info.suffix().compare("zip", Qt::CaseInsensitive) == 0
            ? addZip(path) > 0 : addDll(info.fileName(), contents(path));
        if (!added) failed << info.fileName();
    }
    refresh();
    ask(true);
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Add plugin"), tr("No plugin could be added from %1.").arg(failed.join(", ")));
}

void PluginsPage::toggle(QTreeWidgetItem *item)
{
    const bool on = item->checkState(0) == Qt::Checked;
    if (m_filling || on == item->data(0, OnRole).toBool()) return;
    const QString file = item->data(0, FileRole).toString();
    QDir().mkpath(place(on));
    QFile::remove(location(file, on));
    if (!QFile::rename(location(file, !on), location(file, on)))
        QMessageBox::warning(this, tr("Plugins"), tr("Could not move %1.").arg(file));
    QTimer::singleShot(0, this, [this]() {
        refresh();
        ask(true);
    });
}

void PluginsPage::removePlugin()
{
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item) return;
    if (QMessageBox::question(this, tr("Remove plugin"), tr("Remove the plugin \"%1\"? Its saved data is kept.").arg(item->text(0))) != QMessageBox::Yes)
        return;
    QFile::remove(location(item->data(0, FileRole).toString(), item->data(0, OnRole).toBool()));
    refresh();
    ask(true);
}

void PluginsPage::showMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_list->itemAt(pos);
    if (!item) return;
    m_list->setCurrentItem(item);
    const QString dir = place(item->data(0, OnRole).toBool());
    QMenu menu(this);
    menu.addAction(tr("Show in Explorer"), this, [dir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
    menu.addAction(tr("Remove..."), this, &PluginsPage::removePlugin);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}
