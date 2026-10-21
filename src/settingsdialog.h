#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>

class QCheckBox;
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

    Ui::SettingsDialog *ui;
    QList<QPair<QString, QCheckBox *>> m_patches;
    QLabel *m_account = nullptr;
    QPushButton *m_accountButton = nullptr;
};

#endif
