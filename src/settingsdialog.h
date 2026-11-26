#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void accept() override;

private:
    void showAccount();
    void switchAccount();
#ifndef Q_OS_WIN
    void refreshProtons(const QString &selectPath = {});
#endif

    Ui::SettingsDialog *ui;
    QList<QPair<QString, QCheckBox *>> m_patches;
    QLabel *m_account = nullptr;
    QPushButton *m_accountButton = nullptr;
    QComboBox *m_language = nullptr;
    QComboBox *m_theme = nullptr;
#ifndef Q_OS_WIN
    QComboBox *m_proton = nullptr;
    QPushButton *m_installProtonButton = nullptr;
#endif
    QCheckBox *m_askMapClient = nullptr;
};

#endif
