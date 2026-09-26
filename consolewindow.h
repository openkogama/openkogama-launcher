#ifndef CONSOLEWINDOW_H
#define CONSOLEWINDOW_H

#include <QDateTime>
#include <QProcess>
#include <QTimer>
#include <QWidget>

class QPlainTextEdit;
class QTabWidget;

class ConsoleWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ConsoleWindow(const QString &title, QWidget *parent = nullptr);

    void follow(const QString &label, const QString &path);
    void followNewest(const QString &label, const QString &directory, bool onlyNewLines = false);
    void gameFinished(int exitCode, QProcess::ExitStatus status);

signals:
    void crashed();

private:
    struct Source {
        QPlainTextEdit *view = nullptr;
        QString path;
        QString directory;
        qint64 offset = 0;
        QByteArray partial;
    };

    void addSource(const QString &label, const QString &path, const QString &directory, qint64 offset = 0);
    void poll();
    void append(int source, const QString &line);
    void reportCrash(int source);

    QTabWidget *m_tabs;
    QList<Source> m_sources;
    QTimer m_timer;
    QDateTime m_started = QDateTime::currentDateTime();
    bool m_crashed = false;
};

#endif
