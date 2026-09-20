#include "installdialog.h"
#include "ui_installdialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>

InstallDialog::InstallDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::InstallDialog)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/install.png"));
    m_net = new QNetworkAccessManager(this);
    loadVersions();
    m_net->get(QNetworkRequest(QUrl("https://cdn.openkogama.org/versions.json")));
}

InstallDialog::~InstallDialog()
{
    delete ui;
}

void InstallDialog::onVersionSelected(const QModelIndex &current, const QModelIndex &previous) {
    auto object = versions[current.row()];
    ui->versionValue->setText(object.version);
    ui->releasedValue->setText(object.date);
    ui->unityValue->setText(object.unity);
    ui->backendValue->setText(object.backend);
    ui->downloadValue->setText(object.download);
    ui->installedValue->setText(object.installed);
    ui->shaValue->setText(object.sha);
    ui->versionNameLineEdit->setPlaceholderText(object.version);
}

void InstallDialog::onNetworkReply(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) return;
    QByteArray data = reply->readAll();

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
            QString download = QLocale().formattedDataSize(object.value("zipSize").toInt());
            QString installed = QLocale().formattedDataSize(object.value("unpackedSize").toInt());
            QString sha = object.value("sha256").toString().left(7);
            versions.append({version, date, unity, backend, download, installed, sha});

            model->appendRow({new QStandardItem(version),
                new QStandardItem(date),});
        }
    }
    ui->versionsTreeView->setCurrentIndex(model->index(0,0));
}

void InstallDialog::loadVersions() {
    model = new QStandardItemModel(this);
    model->setHorizontalHeaderLabels({"Version", "Released"});

    ui->versionsTreeView->setModel(model);

    connect(ui->versionsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &InstallDialog::onVersionSelected);
    connect(m_net, &QNetworkAccessManager::finished, this, &InstallDialog::onNetworkReply);

    ui->versionsTreeView->setCurrentIndex(QModelIndex());
    ui->versionsTreeView->clearSelection();
    ui->versionsTreeView->setRootIsDecorated(false);
    ui->versionsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->versionsTreeView->setAlternatingRowColors(true);
    ui->versionsTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
    ui->versionsTreeView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
}
