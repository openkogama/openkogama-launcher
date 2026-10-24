#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settings.h"
#include "discordpresence.h"
#include "clientpatches.h"
#include "account.h"
#include "languages.h"
#include "theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
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
    ui->generalLayout->insertRow(0, tr("Account:"), account);
    connect(m_accountButton, &QPushButton::clicked, this, &SettingsDialog::switchAccount);
    connect(Account::events(), &AccountEvents::changed, this, &SettingsDialog::showAccount);
    showAccount();
    m_language = new QComboBox(ui->generalPage);
    m_language->addItem(tr("System"), QString());
    for (const QString &code : Languages::available())
        m_language->addItem(Languages::nameOf(code), code);
    m_language->setCurrentIndex(qMax(0, m_language->findData(Settings::language())));
    ui->generalLayout->addRow(tr("Language:"), m_language);
    m_theme = new QComboBox(ui->generalPage);
    m_theme->addItem(tr("System"), QString());
    m_theme->addItem(tr("Light"), "light");
    m_theme->addItem(tr("Dark"), "dark");
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(Settings::theme())));
    ui->generalLayout->addRow(tr("Theme:"), m_theme);
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
    m_account->setText(name.isEmpty() ? tr("Not signed in") : name);
    m_accountButton->setText(name.isEmpty() ? tr("Sign in...") : tr("Sign out"));
}

void SettingsDialog::switchAccount()
{
    if (!Account::name().isEmpty()) {
        Account::signOut();
        return;
    }
    QDesktopServices::openUrl(Account::signInPage());
    m_account->setText(tr("Finish in your browser"));
}

void SettingsDialog::accept()
{
    Settings::setNickname(ui->nicknameEdit->text().trimmed());
    Settings::setConsoleOnLaunch(ui->consoleOnLaunchCheck->isChecked());
    Settings::setConsoleOnCrash(ui->consoleOnCrashCheck->isChecked());
    Settings::setDiscordPresence(ui->discordPresenceCheck->isChecked());
    for (const auto &[id, check] : m_patches)
        Settings::setClientPatch(id, check->isChecked());
    const QString language = m_language->currentData().toString();
    if (language != Settings::language()) {
        Settings::setLanguage(language);
        QMessageBox::information(this, tr("Language"), tr("The new language shows after the launcher restarts."));
    }
    const QString theme = m_theme->currentData().toString();
    if (theme != Settings::theme()) {
        Settings::setTheme(theme);
        Theme::use(theme);
    }
    DiscordPresence::refresh();
    QDialog::accept();
}
