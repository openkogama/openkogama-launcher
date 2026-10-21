#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>

namespace ModelPreview {

int cubeCount(const QByteArray &file);
bool hasScale(const QByteArray &file);
float scale(const QByteArray &file);
QImage render(const QByteArray &file, const QSize &size);

}
