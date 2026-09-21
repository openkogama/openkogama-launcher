#include "installdialog.h"
#include "ui_installdialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QDebug>

InstallDialog::InstallDialog(const QByteArray &versionsJson, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::InstallDialog)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/install.png"));
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

QString InstallDialog::instanceName() const {
    QString name = ui->versionNameLineEdit->text().trimmed();
    return name.isEmpty() ? selectedVersion().version : name;
}

void InstallDialog::onVersionSelected(const QModelIndex &current, const QModelIndex &previous) {
    if (!current.isValid()) return;
    auto object = versions[proxy->mapToSource(current).row()];
    ui->versionValue->setText(object.version);
    ui->releasedValue->setText(object.date);
    ui->unityValue->setText(object.unity);
    ui->backendValue->setText(object.backend);
    ui->downloadValue->setText(object.download);
    ui->installedValue->setText(object.installed);
    ui->shaValue->setText(object.sha);
    ui->versionNameLineEdit->setPlaceholderText(object.version);
}

void InstallDialog::populate(const QByteArray &data) {
    QJsonParseError err;
    auto document = QJsonDocument::fromJson(data, &err);
    if (err.error == QJsonParseError::NoError) {

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
            QString installed = QLocale().formattedDataSize(object.value("unpackedSize").toVariant().toLongLong());
            QString sha256 = object.value("sha256").toString();
            QString id = object.value("id").toString();

            QStringList urls;
            for (const auto& u : object.value("urls").toArray())
                urls.append(u.toString());

            versions.append({version, date, unity, backend, download, installed,
                             sha256.left(7), urls, sha256, zipSize, id});

            model->appendRow({new QStandardItem(version),
                new QStandardItem(date),});
        }
    }
    ui->versionsTreeView->setCurrentIndex(proxy->index(0,0));
}

void InstallDialog::loadVersions() {
    model = new QStandardItemModel(this);
    model->setHorizontalHeaderLabels({"Version", "Released"});

    proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);

    ui->versionsTreeView->setModel(proxy);

    connect(ui->versionsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &InstallDialog::onVersionSelected);
    connect(ui->searchLineEdit, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);

    ui->versionsTreeView->setCurrentIndex(QModelIndex());
    ui->versionsTreeView->clearSelection();
    ui->versionsTreeView->setRootIsDecorated(false);
    ui->versionsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->versionsTreeView->setAlternatingRowColors(true);
    ui->versionsTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
    ui->versionsTreeView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
}
