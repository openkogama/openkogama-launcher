#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settings.h"

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    ui->consoleOnLaunchCheck->setChecked(Settings::consoleOnLaunch());
    ui->consoleOnCrashCheck->setChecked(Settings::consoleOnCrash());
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::accept()
{
    Settings::setConsoleOnLaunch(ui->consoleOnLaunchCheck->isChecked());
    Settings::setConsoleOnCrash(ui->consoleOnCrashCheck->isChecked());
    QDialog::accept();
}
