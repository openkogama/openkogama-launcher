#ifndef PATHS_H
#define PATHS_H

#include <QString>

inline QString LocalServerUrl = "http://127.0.0.1:8080";
inline int LocalHttpPort = 8080;
inline int LocalPhotonPort = 5055;

namespace Paths {

QString data();
QString instances();
QString mods();
QString player();
QString server();
QString serverData();

}

#endif // PATHS_H
