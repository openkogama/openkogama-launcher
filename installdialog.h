#ifndef INSTALLDIALOG_H
#define INSTALLDIALOG_H

#include <QDialog>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QUrl>

namespace Ui {
class InstallDialog;
}

struct Version {
    QString version;
    QString date;
    QString unity;
    QString backend;
    QString download;
    QString installed;
    QString sha;
    QStringList urls;
    QString sha256;
    qint64 zipSize = 0;
    QString id;
};

class InstallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InstallDialog(const QByteArray &versionsJson, QWidget *parent = nullptr);
    ~InstallDialog();
    Version selectedVersion() const;
    QString instanceName() const;

private:
    Ui::InstallDialog *ui;
    QStandardItemModel *model;
    QSortFilterProxyModel *proxy;
    void loadVersions();
    void populate(const QByteArray &data);
    void onVersionSelected(const QModelIndex &current, const QModelIndex &previous);
    QList<Version> versions;
};

#endif // INSTALLDIALOG_H
