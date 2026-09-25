#ifndef CENTEREDDELEGATE_H
#define CENTEREDDELEGATE_H

#include <QLineEdit>
#include <QStyledItemDelegate>

class CenteredDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(editor))
            line->setAlignment(Qt::AlignCenter);
        return editor;
    }
};

#endif // CENTEREDDELEGATE_H
