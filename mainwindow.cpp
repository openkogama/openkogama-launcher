#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "installdialog.h"
#include <QDesktopServices>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("OpenKogama Launcher");

    connect(ui->actionInstall, &QAction::triggered, this, &MainWindow::onInstallTriggered);
    connect(ui->actionDiscord, &QAction::triggered, this, &MainWindow::onDiscordTriggered);
    ui->actionDiscord->setIcon(QIcon(":/discord.png"));
    ui->actionInstall->setIcon(QIcon(":/install.png"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onInstallTriggered() {
    InstallDialog dialog(this);
    dialog.exec();
}

void MainWindow::onDiscordTriggered() {
    QDesktopServices::openUrl(QUrl("https://discord.gg/u6tKuP3k4M"));
}
