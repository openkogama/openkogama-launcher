#ifndef GAMERUNTIME_H
#define GAMERUNTIME_H

#include <QString>
#include <QStringList>

class QProcess;

// Everything that differs between Windows and the platforms where the Windows
// game builds have to run through a compatibility layer such as Wine.
namespace GameRuntime {

bool isWindowsExecutable(const QString &path);
bool needsWine(const QString &program);
bool wineAvailable();
QString wineBinary();
QString winePrefix();

// The home and temp folder as the game sees them: C:/Users/<name> on Windows and
// the same folder inside the Wine prefix elsewhere.
QString gameHome();
QString gameTemp();

// A local path in the form the game understands, e.g. Z:\home\me\world.
QString windowsPath(const QString &path);

QString serverExecutable();
QString playerExecutable();
QString serverProgram();
QString playerProgram();
bool hasServer();
bool hasPlayer();

bool isServerProgram(const QString &program);

// Starts a Windows executable through Wine on platforms without Windows and
// keeps console windows hidden on Windows. Call after setProgram() and
// setArguments().
void prepare(QProcess *process);

QString registryValue(const QString &key, const QString &name);
bool setRegistryValue(const QString &key, const QString &name, const QString &value);

}

#endif // GAMERUNTIME_H
