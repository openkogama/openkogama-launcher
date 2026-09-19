#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHeaderView>
#include <QStandardItemModel>

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

    model->appendRow({new QStandardItem("1.25.13.285"),
                      new QStandardItem("19/08/2015"),});

    ui->versionsTreeView->setModel(model);
    ui->versionsTreeView->setCurrentIndex(QModelIndex());
    ui->versionsTreeView->clearSelection();
    ui->versionsTreeView->setRootIsDecorated(false);
    ui->versionsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->versionsTreeView->setAlternatingRowColors(true);
    ui->versionsTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
}



