#include "installdialog.h"
#include "theme.h"
#include "ui_installdialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QDebug>
#include <algorithm>

namespace {
constexpr int MaxListed = 3;
}

InstallDialog::InstallDialog(const QByteArray &versionsJson, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::InstallDialog)
{
    ui->setupUi(this);
    setWindowIcon(Theme::icon(":/install.png"));
    loadVersions();
    populate(versionsJson);
}

InstallDialog::~InstallDialog()
{
    delete ui;
}

Version InstallDialog::selectedVersion() const {
    QModelIndex current = ui->versionsTreeView->currentIndex();
    if (!current.isValid()) return {};
    return versions[proxy->mapToSource(current).row()];
}

QList<Version> InstallDialog::selectedVersions() const {
    QList<int> rows;
    for (const QModelIndex &index : ui->versionsTreeView->selectionModel()->selectedRows())
        rows.append(proxy->mapToSource(index).row());
    std::sort(rows.begin(), rows.end());
    QList<Version> chosen;
    for (int row : std::as_const(rows)) chosen.append(versions[row]);
    if (chosen.isEmpty() && ui->versionsTreeView->currentIndex().isValid()) chosen.append(selectedVersion());
    return chosen;
}

QString InstallDialog::instanceName() const {
    QString name = ui->versionNameLineEdit->text().trimmed();
    return name.isEmpty() ? selectedVersion().version : name;
}

void InstallDialog::onVersionSelected(const QModelIndex &current, const QModelIndex &previous) {
    if (!current.isValid()) return;
    showSelection();
}

void InstallDialog::showVersion(const Version &version) {
    ui->versionValue->setText(version.version);
    ui->releasedValue->setText(version.date);
    ui->unityValue->setText(version.unity);
    ui->backendValue->setText(version.backend);
    ui->downloadValue->setText(version.download);
    ui->installedValue->setText(version.installed);
    ui->shaValue->setText(version.sha);
    ui->versionNameLineEdit->setPlaceholderText(version.version);
}

void InstallDialog::showSelection() {
    const QList<Version> chosen = selectedVersions();
    ui->versionNameLineEdit->setEnabled(chosen.size() <= 1);
    if (chosen.size() == 1) showVersion(chosen.first());
    if (chosen.size() <= 1) return;
    qint64 download = 0;
    qint64 unpacked = 0;
    QStringList unity;
    QStringList backend;
    for (const Version &version : chosen) {
        download += version.zipSize;
        unpacked += version.unpackedSize;
        if (!unity.contains(version.unity)) unity.append(version.unity);
        if (!backend.contains(version.backend)) backend.append(version.backend);
    }
    const auto shortList = [](const QStringList &items) {
        return items.size() <= MaxListed ? items.join(", ") : tr("%1 and %n more", nullptr, int(items.size() - MaxListed)).arg(items.mid(0, MaxListed).join(", "));
    };
    ui->versionValue->setText(tr("%n version(s)", nullptr, int(chosen.size())));
    ui->releasedValue->setText(chosen.last().date == chosen.first().date ? chosen.first().date : tr("%1 to %2").arg(chosen.last().date, chosen.first().date));
    ui->unityValue->setText(shortList(unity));
    ui->backendValue->setText(backend.join(", "));
    ui->downloadValue->setText(QLocale().formattedDataSize(download));
    ui->installedValue->setText(QLocale().formattedDataSize(unpacked));
    ui->shaValue->setText(QString());
}

QList<Version> InstallDialog::parse(const QByteArray &data) {
    QList<Version> parsed;
    QJsonParseError err;
    auto document = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return parsed;

    const auto array = document.object().value("versions").toArray();

    for (const auto& entry : array) {
        auto object = entry.toObject();
        QString version = object.value("version").toString();
        int timestamp = object.value("timestamp").toInt();
        QString date = QDateTime::fromSecsSinceEpoch(timestamp).toString("dd/MM/yyyy");
        QString unity = object.value("unityVersion").toString();
        QString backend;
        if (object.value("il2cpp").toBool()) backend = "IL2CPP";
        else backend = "Mono";
        qint64 zipSize = object.value("zipSize").toVariant().toLongLong();
        QString download = QLocale().formattedDataSize(zipSize);
        qint64 unpackedSize = object.value("unpackedSize").toVariant().toLongLong();
        QString installed = QLocale().formattedDataSize(unpackedSize);
        QString sha256 = object.value("sha256").toString();
        QString id = object.value("id").toString();

        QStringList urls;
        for (const auto& u : object.value("urls").toArray())
            urls.append(u.toString());

        parsed.append({version, date, unity, backend, download, installed,
                       sha256.left(7), urls, sha256, zipSize, id, unpackedSize});
    }
    return parsed;
}

void InstallDialog::populate(const QByteArray &data) {
    versions = parse(data);
    for (const Version &version : std::as_const(versions))
        model->appendRow({new QStandardItem(version.version), new QStandardItem(version.date)});
    ui->versionsTreeView->setCurrentIndex(proxy->index(0,0));
}

void InstallDialog::loadVersions() {
    model = new QStandardItemModel(this);
    model->setHorizontalHeaderLabels({tr("Version"), tr("Released")});

    proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);

    ui->versionsTreeView->setModel(proxy);

    ui->versionsTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(ui->versionsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &InstallDialog::onVersionSelected);
    connect(ui->versionsTreeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &InstallDialog::showSelection);
    connect(ui->searchLineEdit, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);

    ui->versionsTreeView->setCurrentIndex(QModelIndex());
    ui->versionsTreeView->clearSelection();
    ui->versionsTreeView->setRootIsDecorated(false);
    ui->versionsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->versionsTreeView->setAlternatingRowColors(true);
    ui->versionsTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
    ui->versionsTreeView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
}
