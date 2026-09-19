#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QDirIterator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("OpenKogama Launcher");

    // ui->versionComboBox->addItem("1.25.13.292");
    // ui->versionComboBox->addItem("1.25.13.285");
    loadVersions();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onVersionSelected(const QModelIndex &current, const QModelIndex &previous) {
    auto object = versions[current.row()];
    ui->versionValue->setText(object.version);
    ui->releasedValue->setText(object.date);
    ui->unityValue->setText(object.unity);
    ui->backendValue->setText(object.backend);
    ui->downloadValue->setText(object.download);
    ui->installedValue->setText(object.installed);
    ui->shaValue->setText(object.sha);
}

void MainWindow::loadVersions() {
    auto *model = new QStandardItemModel(this);
    model->setHorizontalHeaderLabels({"Version", "Released"});

    QFile file(":/versions.json");
    if (file.open(QIODevice::ReadOnly) == false) file.errorString();
    QByteArray data = file.readAll();

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
            QString sha = object.value("sha256").toString().left(16) + "...";
            versions.append({version, date, unity, backend, download, installed, sha});

            model->appendRow({new QStandardItem(version),
                new QStandardItem(date),});
        }
    }

    ui->versionsTreeView->setModel(model);
    connect(ui->versionsTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onVersionSelected);
    ui->versionsTreeView->setCurrentIndex(QModelIndex());
    ui->versionsTreeView->clearSelection();
    ui->versionsTreeView->setRootIsDecorated(false);
    ui->versionsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->versionsTreeView->setAlternatingRowColors(true);
    ui->versionsTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
    ui->versionsTreeView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
}



