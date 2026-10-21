#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settings.h"
#include "discordpresence.h"
#include "clientpatches.h"
#include "account.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    auto *account = new QWidget(ui->generalPage);
    auto *accountRow = new QHBoxLayout(account);
    accountRow->setContentsMargins(0, 0, 0, 0);
    m_account = new QLabel(account);
    m_accountButton = new QPushButton(account);
    accountRow->addWidget(m_account, 1);
    accountRow->addWidget(m_accountButton);
    ui->generalLayout->insertRow(0, "Account:", account);
    connect(m_accountButton, &QPushButton::clicked, this, &SettingsDialog::switchAccount);
    connect(Account::events(), &AccountEvents::changed, this, &SettingsDialog::showAccount);
    showAccount();
    ui->nicknameEdit->setText(Settings::nickname());
    ui->consoleOnLaunchCheck->setChecked(Settings::consoleOnLaunch());
    ui->consoleOnCrashCheck->setChecked(Settings::consoleOnCrash());
    ui->discordPresenceCheck->setChecked(Settings::discordPresence());
    for (const ClientPatch &patch : ClientPatches::all()) {
        auto *check = new QCheckBox(patch.name, ui->clientPatchesPage);
        check->setToolTip(patch.description);
        check->setChecked(Settings::clientPatch(patch.id));
        ui->clientPatchesLayout->addWidget(check);
        m_patches.append({patch.id, check});
    }
    ui->clientPatchesLayout->addStretch();
    adjustSize();
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::showAccount()
{
    const QString name = Account::name();
    m_account->setText(name.isEmpty() ? "Not signed in" : name);
    m_accountButton->setText(name.isEmpty() ? "Sign in..." : "Sign out");
}

void SettingsDialog::switchAccount()
{
    if (!Account::name().isEmpty()) {
        Account::signOut();
        return;
    }
    QDesktopServices::openUrl(Account::signInPage());
    m_account->setText("Finish in your browser");
}

void SettingsDialog::accept()
{
    Settings::setNickname(ui->nicknameEdit->text().trimmed());
    Settings::setConsoleOnLaunch(ui->consoleOnLaunchCheck->isChecked());
    Settings::setConsoleOnCrash(ui->consoleOnCrashCheck->isChecked());
    Settings::setDiscordPresence(ui->discordPresenceCheck->isChecked());
    for (const auto &[id, check] : m_patches)
        Settings::setClientPatch(id, check->isChecked());
    DiscordPresence::refresh();
    QDialog::accept();
}
