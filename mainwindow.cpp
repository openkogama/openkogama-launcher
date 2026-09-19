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

void MainWindow::loadVersions() {
    auto *model = new QStandardItemModel(this);
    model->setHorizontalHeaderLabels({"Version", "Released"});

    QFile file(":/versions.json");
    if (file.open(QIODevice::ReadOnly) == false) file.errorString();
    QByteArray data = file.readAll();

    QJsonParseError err;
    auto document = QJsonDocument::fromJson(data, &err);
    if (err.error == QJsonParseError::NoError) {

        const auto entry = document.object().value("versions").toArray();

        for (const auto& object : entry) {
            QString version = object.toObject().value("version").toString();
            int timestamp = object.toObject().value("timestamp").toInt();
            QString date = QDateTime::fromSecsSinceEpoch(timestamp).toString("dd/MM/yyyy");

            model->appendRow({new QStandardItem(version),
                new QStandardItem(date),});
        }
    }

    ui->versionsTreeView->setModel(model);
    ui->versionsTreeView->setCurrentIndex(QModelIndex());
    ui->versionsTreeView->clearSelection();
    ui->versionsTreeView->setRootIsDecorated(false);
    ui->versionsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->versionsTreeView->setAlternatingRowColors(true);
    ui->versionsTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
}



