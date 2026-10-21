#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class AccountEvents : public QObject
{
    Q_OBJECT

signals:
    void changed();
};

namespace Account {

struct Reply
{
    int status = 0;
    QJsonObject body;

    bool ok() const { return status == 200; }
    QString error() const;
};

AccountEvents *events();
QString name();
QUrl signInPage();
Reply finishSignIn(const QString &code);
Reply ticket(const QString &server);
void signOut();

}
