#pragma once

#include <QWidget>

class QJsonArray;
class QLabel;
class QNetworkAccessManager;
class QTreeWidget;
class QTreeWidgetItem;

class PluginsPage : public QWidget
{
    Q_OBJECT

public:
    explicit PluginsPage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void refresh();
    void ask(bool reload);
    void showStatus(const QJsonArray &files, bool running);
    void addPlugins();
    void install(const QStringList &paths);
    void toggle(QTreeWidgetItem *item);
    void removePlugin();
    void showMenu(const QPoint &pos);

    QNetworkAccessManager *m_net;
    QTreeWidget *m_list;
    QLabel *m_empty;
    QLabel *m_status;
    bool m_filling = false;
};
