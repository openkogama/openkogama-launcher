#ifndef INSTALLPROGRESSDIALOG_H
#define INSTALLPROGRESSDIALOG_H

#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include "installdialog.h"

namespace Ui {
class InstallProgressDialog;
}

class InstallProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InstallProgressDialog(const Version &version, const QString &name, QWidget *parent = nullptr);
    ~InstallProgressDialog();

protected:
    void reject() override;

private:
    void startDownload();
    void consume();
    void onProgress(qint64 received, qint64 total);
    void onFinished();
    void extract();
    void onExtracted();

    Ui::InstallProgressDialog *ui;
    Version m_version;
    QString m_name;
    QString m_dest;
    QNetworkAccessManager *m_net;
    QNetworkReply *m_reply = nullptr;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QFutureWatcher<bool> *m_watcher;
    int m_urlIndex = 0;
    bool m_cancelled = false;
};

#endif // INSTALLPROGRESSDIALOG_H
