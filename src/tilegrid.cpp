#include "tilegrid.h"
#include "centereddelegate.h"
#include <QCoreApplication>
#include <QHoverEvent>
#include <QListWidget>
#include <QScrollBar>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <algorithm>

namespace {
constexpr int Spacing = 16;
constexpr int TextHeight = 40;
}

namespace TileGrid {

void setup(QListWidget *list, QObject *resizeFilter)
{
    list->setItemDelegate(new CenteredDelegate(list));
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setUniformItemSizes(true);
    list->setFrameShape(QFrame::NoFrame);
    list->setFocusPolicy(Qt::StrongFocus);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setTextElideMode(Qt::ElideRight);
    list->installEventFilter(resizeFilter);
    QObject::connect(list->verticalScrollBar(), &QScrollBar::valueChanged, list, [list]() {
        QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF(-1, -1));
        QCoreApplication::sendEvent(list->viewport(), &leave);
    });
}

void fit(QListWidget *list, const QSize &tile)
{
    int available = list->contentsRect().width() - list->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, list->verticalScrollBar()) - 1;
    int columns = std::max(1, (available + Spacing) / (tile.width() + Spacing));
    int column = std::max(tile.width(), available / columns);
    int width = std::max(tile.width(), (column * columns - Spacing * (columns - 1)) / columns);
    QSize scaled(width, width * tile.height() / tile.width());
    QSize grid(column, scaled.height() + TextHeight + Spacing);
    if (list->iconSize() != scaled) list->setIconSize(scaled);
    if (list->gridSize() != grid) list->setGridSize(grid);
}

QPixmap backdrop(const QSize &size, const QColor &background)
{
    QPixmap pixmap(size);
    pixmap.fill(background);
    QPainter painter(&pixmap);
    painter.fillRect(pixmap.rect(), QColor(0, 0, 0, background.lightness() > 128 ? 15 : 46));
    return pixmap;
}

QPixmap placeholder(const QSize &size, const QColor &background)
{
    QPixmap pixmap = backdrop(size, background);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool light = background.lightness() > 128;
    const qreal scale = std::min(size.width(), size.height()) * 0.4 / 24;
    painter.translate((size.width() - 24 * scale) / 2, (size.height() - 24 * scale) / 2);
    painter.scale(scale, scale);
    painter.setPen(QPen(light ? QColor(0, 0, 0, 64) : QColor(255, 255, 255, 77), 1.6, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    QPainterPath cube;
    cube.moveTo(12, 3);
    cube.lineTo(3, 7.5);
    cube.lineTo(3, 16.5);
    cube.lineTo(12, 21);
    cube.lineTo(21, 16.5);
    cube.lineTo(21, 7.5);
    cube.closeSubpath();
    cube.moveTo(3, 7.5);
    cube.lineTo(12, 12);
    cube.lineTo(21, 7.5);
    cube.moveTo(12, 12);
    cube.lineTo(12, 21);
    painter.drawPath(cube);
    return pixmap;
}

QPixmap cover(const QPixmap &image, const QSize &size)
{
    QPixmap scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return scaled.copy((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2, size.width(), size.height());
}

QIcon icon(const QPixmap &pixmap)
{
    QIcon result;
    result.addPixmap(pixmap, QIcon::Normal);
    result.addPixmap(pixmap, QIcon::Selected);
    return result;
}

}
