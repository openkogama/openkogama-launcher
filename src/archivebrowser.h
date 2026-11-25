#ifndef ARCHIVEBROWSER_H
#define ARCHIVEBROWSER_H

#include "links.h"

#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QUrl>
#include <QVector>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTimer;

class ArchiveBrowser : public QWidget
{
    Q_OBJECT

public:
    enum Kind { Maps, Avatars, Models };

    explicit ArchiveBrowser(Kind kind, QWidget *parent = nullptr);

    void setEndpoints(const QUrl &archiveUrl, const QUrl &metaUrl = {});
    void ensureLoaded();
    void reload();

signals:
    void importRequested(const ImportRequest &request, const QUrl &thumbnailUrl);
    void clientForYearRequested(const QString &published, const QString &mapName);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry {
        int id = 0;
        int position = 0;
        QString name;
        QString author;
        QString site;
        QString type;
        QString published;
        QString format;
        QString image;
        QString sha256;
        QString searchText;
        qint64 plays = 0;
        qint64 likes = 0;
        qint64 size = 0;
        quint32 maps = 0;
        quint32 cubes = 0;
        float scale = 0.0f;
        quint16 pack = 0;
        quint32 offset = 0;
        quint32 length = 0;
        quint8 sources = 0;
        bool hasImage = false;
    };

    void fetchLatest();
    void loadMapArchive();
    void loadCardArchive();
    void loadCardChunk(int chunkIndex, std::function<void()> done = {});
    void parseMapIndex(const QByteArray &data);
    void parseCardChunk(int chunkIndex, const QByteArray &cards);
    void refreshTypeFilter();

    void applyFilter();
    void appendPage();
    void updateActions();
    void showContextMenu(const QPoint &pos);
    void requestImportSelection();
    void requestImportEntries(const QVector<int> &entryIndices);
    void requestMapDetails(const QVector<Entry> &entries);
    void fetchNextThumbnail();
    void loadThumbnailFor(QListWidgetItem *item);
    QUrl thumbnailUrlFor(const Entry &entry) const;
    QString subtitleFor(const Entry &entry) const;
    QString detailFor(const Entry &entry) const;
    QString cachePrefix() const;

    QByteArray readCache(const QString &fileName) const;
    void writeCache(const QString &fileName, const QByteArray &data) const;
    void pruneCache(const QString &prefix, const QString &keepPrefix) const;

    Kind m_kind;
    QUrl m_archiveUrl;
    QUrl m_metaUrl;
    QUrl m_buildUrl;
    QUrl m_imagesUrl;
    QString m_build;
    QString m_indexPath;
    QString m_detailsPath;
    QString m_cardsPath;
    QString m_titlesPath;
    QString m_imagePath;
    int m_detailsPerFile = 1024;
    int m_cardRecordSize = 54;
    int m_cardsPerFile = 16384;
    int m_totalCards = 0;
    int m_loadedCardChunks = 0;
    int m_totalCardChunks = 0;
    bool m_loadingChunk = false;

    QNetworkAccessManager *m_net;
    QNetworkAccessManager *m_thumbNet;
    QLineEdit *m_search;
    QComboBox *m_filter;
    QComboBox *m_yearFilter = nullptr;
    QComboBox *m_sort;
    QLabel *m_status;
    QListWidget *m_list;
    QLabel *m_selected;
    QLabel *m_detail;
    QPushButton *m_addButton;
    QTimer *m_searchTimer;

    QStringList m_mapTitles;
    QJsonObject m_metaRows;
    QSet<QString> m_types;
    QVector<Entry> m_entries;
    QVector<int> m_filtered;
    int m_shown = 0;
    int m_generation = 0;
    int m_activeThumbnails = 0;
    bool m_loaded = false;
    bool m_loading = false;
    bool m_importing = false;

    QHash<int, QJsonArray> m_mapDetails;
    QHash<QString, QIcon> m_thumbnailCache;
    QQueue<QPair<int, QString>> m_thumbnailQueue;
    QSet<QString> m_thumbnailQueued;
    QIcon m_placeholder;
};

#endif // ARCHIVEBROWSER_H
