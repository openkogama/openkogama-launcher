#ifndef CENTEREDDELEGATE_H
#define CENTEREDDELEGATE_H

#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <algorithm>

class CenteredDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr int SubtitleRole = Qt::UserRole + 20;

    void setBadgeRole(int role) { m_badgeRole = role; }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(editor))
            line->setAlignment(Qt::AlignCenter);
        return editor;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem fit = fitted(option);
        const bool selected = fit.state & QStyle::State_Selected;
        const bool hovered = fit.state & QStyle::State_MouseOver;
        fit.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver);
        QStyledItemDelegate::paint(painter, fit, index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath corners;
        corners.addRect(QRectF(fit.rect));
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(fit.rect), Radius + 1, Radius + 1);
        painter->fillPath(corners.subtracted(rounded), option.palette.color(QPalette::Window));

        const QString subtitle = index.data(SubtitleRole).toString();
        if (!subtitle.isEmpty()) {
            const QRect label = fit.rect.adjusted(4, fit.decorationSize.height(), -4, 0);
            const int line = fit.fontMetrics.height();
            const QRect title(label.left(), label.center().y() - line, label.width(), line);
            painter->setFont(fit.font);
            painter->setPen(fit.palette.color(QPalette::WindowText));
            painter->drawText(title, Qt::AlignCenter, fit.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, title.width()));
            painter->setPen(fit.palette.color(QPalette::Disabled, QPalette::WindowText));
            painter->drawText(title.translated(0, line), Qt::AlignCenter, fit.fontMetrics.elidedText(subtitle, Qt::ElideRight, title.width()));
        }
        painter->setRenderHint(QPainter::Antialiasing);
        const QColor accent = option.palette.color(QPalette::Active, QPalette::Highlight);
        const QRectF frame = QRectF(fit.rect).adjusted(1, 1, -1, -1);
        if (selected) {
            painter->setPen(QPen(accent, 2));
            painter->drawRoundedRect(frame, Radius, Radius);
        } else if (hovered) {
            painter->setPen(QPen(option.palette.color(QPalette::Mid), 1));
            painter->drawRoundedRect(frame, Radius, Radius);
        }

        if (m_badgeRole >= 0 && index.data(m_badgeRole).toBool()) {
            const QRectF badge(fit.rect.right() - 27, fit.rect.top() + 5, 22, 22);
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent);
            painter->drawEllipse(badge);
            QPainterPath check;
            check.moveTo(badge.left() + 6, badge.center().y());
            check.lineTo(badge.left() + 10, badge.center().y() + 4);
            check.lineTo(badge.right() - 6, badge.top() + 7);
            painter->setPen(QPen(option.palette.color(QPalette::HighlightedText), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(check);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (auto *view = qobject_cast<const QListView *>(option.widget); view && view->gridSize().isValid())
            return view->gridSize();
        return QStyledItemDelegate::sizeHint(option, index);
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto *view = qobject_cast<const QListView *>(option.widget);
        if (!view || !view->gridSize().isValid()) {
            QStyledItemDelegate::updateEditorGeometry(editor, option, index);
            return;
        }
        const QRect label = fitted(option).rect.adjusted(4, option.decorationSize.height(), -4, 0);
        const int height = editor->sizeHint().height();
        editor->setGeometry(label.left(), label.center().y() - height / 2, label.width(), height);
    }

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!index.data(SubtitleRole).toString().isEmpty())
            option->text.clear();
    }

private:
    static QStyleOptionViewItem fitted(const QStyleOptionViewItem &option)
    {
        QStyleOptionViewItem fit = option;
        auto *view = qobject_cast<const QListView *>(option.widget);
        if (!view || !view->gridSize().isValid()) return fit;
        const QSize grid = view->gridSize();
        const int tile = view->iconSize().width();
        const int columns = std::max(1, view->viewport()->width() / grid.width());
        const int column = std::clamp(qRound(double(option.rect.left()) / grid.width()), 0, columns - 1);
        const int left = columns > 1 ? column * (columns * grid.width() - tile) / (columns - 1) : 0;
        fit.rect = QRect(left, option.rect.top(), tile, grid.height() - Gap);
        return fit;
    }

    static constexpr int Gap = 16;
    static constexpr int Radius = 4;
    int m_badgeRole = -1;
};

#endif // CENTEREDDELEGATE_H
