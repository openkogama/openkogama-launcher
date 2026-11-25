#ifndef GAMERUNTIME_H
#define GAMERUNTIME_H

#include <QList>
#include <QString>

class QProcess;
class QWidget;

namespace GameRuntime {

struct ProtonBuild {
    QString name;
    QString path;
};

bool needsWine(const QString &program);
bool wineAvailable();
bool ensureAvailable(QWidget *parent);
bool installProton(QWidget *parent, QString *installedPath = nullptr);
QList<ProtonBuild> installedProtons();
QString activeProton();

QString gameHome();
QString gameTemp();
QString windowsPath(const QString &path);

QString serverExecutable();
QString serverProgram();
bool hasServer();
bool isServerProgram(const QString &program);

void prepare(QProcess *process);

QString registryValue(const QString &key, const QString &name);
bool setRegistryValue(const QString &key, const QString &name, const QString &value);

}

#endif // GAMERUNTIME_H
