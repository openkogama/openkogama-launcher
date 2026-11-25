#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include "links.h"

#include <QFrame>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVector>

class ContentImporter;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

struct DownloadEntry {
    enum State { Queued, Active, Completed, Failed, Cancelled };

    int jobId = 0;
    ImportRequest request;
    QUrl thumbnailUrl;
    QString clientVersion;
    State state = Queued;
    int percent = 0;
    QString error;
    int importedId = 0;
    QPointer<ContentImporter> importer;
};

class DownloadManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxConcurrent = 3;

    explicit DownloadManager(QObject *parent = nullptr);

    int enqueue(const ImportRequest &request, const QUrl &thumbnailUrl, const QString &clientVersion);
    void cancel(int jobId);
    void cancelAll();
    void retry(int jobId);
    void clearFinished();

    int activeCount() const;
    int runningCount() const;
    int queuedCount() const;
    const QVector<DownloadEntry> &entries() const { return m_entries; }
    const DownloadEntry *entryById(int jobId) const;

signals:
    void changed();
    void jobAdded(int jobId);
    void jobUpdated(int jobId);
    void listReset();
    void jobCompleted(const QString &kind, int importedId, const QUrl &thumbnailUrl);
    void batchSettled(const QString &kind, int lastImportedId);

private:
    void pump();
    void startJob(int index);
    int indexOf(int jobId) const;
    void scheduleSettle(const QString &kind, int importedId);
    void flushSettled();

    QVector<DownloadEntry> m_entries;
    int m_nextId = 1;
    int m_running = 0;
    QHash<QString, int> m_pendingSettle;
    QTimer *m_settleTimer;
};

class DownloadsPopup : public QFrame
{
    Q_OBJECT

public:
    explicit DownloadsPopup(DownloadManager *manager, QWidget *parent = nullptr);

    void showBelow(QWidget *anchor);

signals:
    void showRequested(const QString &kind, int importedId);

private:
    struct RowWidgets {
        QWidget *container = nullptr;
        QLabel *title = nullptr;
        QLabel *badge = nullptr;
        QLabel *status = nullptr;
        QProgressBar *progress = nullptr;
        QPushButton *action = nullptr;
    };

    void rebuild();
    void addRow(const DownloadEntry &entry);
    void updateRow(int jobId);
    void updateHeader();

    DownloadManager *m_manager;
    QLabel *m_title;
    QPushButton *m_clearButton;
    QPushButton *m_cancelAllButton;
    QLabel *m_empty;
    QScrollArea *m_scroll;
    QWidget *m_listWidget;
    QVBoxLayout *m_listLayout;
    QHash<int, RowWidgets> m_rows;
};

#endif // DOWNLOADMANAGER_H
