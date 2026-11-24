#ifndef GAMERUNTIME_H
#define GAMERUNTIME_H

#include <QString>

class QProcess;

namespace GameRuntime {

bool needsWine(const QString &program);
bool wineAvailable();

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
