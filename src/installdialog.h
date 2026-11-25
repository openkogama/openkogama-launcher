#ifndef INSTALLDIALOG_H
#define INSTALLDIALOG_H

#include <QDialog>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QUrl>
#include <optional>

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
    qint64 unpackedSize = 0;
    qint64 timestamp = 0;
};

class InstallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InstallDialog(const QByteArray &versionsJson, QWidget *parent = nullptr);
    ~InstallDialog();
    Version selectedVersion() const;
    QList<Version> selectedVersions() const;
    QString instanceName() const;
    static QList<Version> parse(const QByteArray &versionsJson);
    static int yearOf(const Version &version);
    static std::optional<Version> bestForDate(const QList<Version> &versions, const QString &isoDate);

private:
    Ui::InstallDialog *ui;
    QStandardItemModel *model;
    QSortFilterProxyModel *proxy;
    void loadVersions();
    void populate(const QByteArray &data);
    void onVersionSelected(const QModelIndex &current, const QModelIndex &previous);
    void showSelection();
    void showVersion(const Version &version);
    QList<Version> versions;
};

#endif // INSTALLDIALOG_H
