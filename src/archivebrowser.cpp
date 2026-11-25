#include "archivebrowser.h"
#include "centereddelegate.h"
#include "paths.h"
#include "tilegrid.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace {

const QUrl DefaultMapsArchive("https://cdn.openkogama.org/maps/v1/");
const QUrl DefaultAvatarsArchive("https://cdn.openkogama.org/avatars/v1/");
const QUrl DefaultAvatarsMeta("https://openkogama.org/avatars/meta.json");
const QUrl DefaultModelsArchive("https://cdn.openkogama.org/models/v2/");
const QUrl DefaultModelsMeta("https://openkogama.org/models/meta.json");

constexpr int IndexRole = Qt::UserRole;
constexpr int ThumbnailRole = Qt::UserRole + 1;
constexpr int PageSize = 60;
constexpr int MaxConcurrentThumbnails = 6;
constexpr int MaxCachedThumbnails = 500;
constexpr int SearchDebounceMs = 150;
constexpr int NetworkTimeoutMs = 30000;
constexpr int ThumbnailTimeoutMs = 10000;
constexpr int ScrollThreshold = 240;
constexpr int ModelSearchChunks = 3; // Covers top 49,152 models (all 37k PNGs + all curated meta.json entries)

QString cacheDir()
{
    return Paths::data() + "/cache/archive";
}

QString formatCount(qint64 value)
{
    if (value >= 1000000)
        return QString::number(double(value) / 1000000.0, 'f', value >= 10000000 ? 0 : 1) + "M";
    if (value >= 1000)
        return QString::number(double(value) / 1000.0, 'f', value >= 10000 ? 0 : 1) + "K";
    return QString::number(value);
}

QString formatSize(qint64 bytes)
{
    if (bytes >= 1000000)
        return QString::number(double(bytes) / 1000000.0, 'f', 1) + " MB";
    return QString::number(qMax<qint64>(1, (bytes + 500) / 1000)) + " KB";
}

QNetworkRequest archiveRequest(const QUrl &url, int timeoutMs = NetworkTimeoutMs)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenKogama Launcher"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(timeoutMs);
    return request;
}

QString hexChunk(int index)
{
    return QStringLiteral("%1").arg(index, 3, 16, QLatin1Char('0'));
}

QString capitalize(const QString &text)
{
    return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1);
}

float readLittleFloat(const uchar *src)
{
    const quint32 bits = qFromLittleEndian<quint32>(src);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return float(std::round(double(value) * 10000.0) / 10000.0);
}

}

ArchiveBrowser::ArchiveBrowser(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_archiveUrl(kind == Maps ? DefaultMapsArchive : kind == Avatars ? DefaultAvatarsArchive : DefaultModelsArchive)
    , m_metaUrl(kind == Avatars ? DefaultAvatarsMeta : kind == Models ? DefaultModelsMeta : QUrl())
    , m_net(new QNetworkAccessManager(this))
    , m_thumbNet(new QNetworkAccessManager(this))
    , m_search(new QLineEdit(this))
    , m_filter(new QComboBox(this))
    , m_sort(new QComboBox(this))
    , m_status(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_selected(new QLabel(this))
    , m_detail(new QLabel(this))
    , m_addButton(new QPushButton(this))
    , m_searchTimer(new QTimer(this))
{
    setObjectName(kind == Maps ? "mapsArchivePage" : kind == Avatars ? "avatarsArchivePage" : "modelsArchivePage");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    m_search->setObjectName(kind == Maps ? "archiveMapSearch" : kind == Avatars ? "archiveAvatarSearch" : "archiveModelSearch");
    m_search->setPlaceholderText(kind == Maps ? tr("Search archived maps...")
        : kind == Avatars ? tr("Search archived avatars...") : tr("Search archived models..."));
    m_search->setAccessibleName(m_search->placeholderText());
    m_search->setClearButtonEnabled(true);

    m_filter->setObjectName(kind == Maps ? "archiveMapRegionFilter" : kind == Avatars ? "archiveAvatarTypeFilter" : "archiveModelTypeFilter");
    if (kind == Maps) {
        m_filter->addItem(tr("All regions"), QString());
        m_filter->addItem(QStringLiteral("www"), QStringLiteral("www"));
        m_filter->addItem(QStringLiteral("br"), QStringLiteral("br"));
        m_filter->addItem(QStringLiteral("friends"), QStringLiteral("friends"));
    } else {
        m_filter->addItem(tr("All types"), QString());
    }

    m_sort->setObjectName(kind == Maps ? "archiveMapSort" : kind == Avatars ? "archiveAvatarSort" : "archiveModelSort");
    if (kind == Maps) {
        m_sort->addItem(tr("Most played"), QStringLiteral("plays"));
        m_sort->addItem(tr("Most liked"), QStringLiteral("likes"));
        m_sort->addItem(tr("Newest"), QStringLiteral("new"));
        m_sort->addItem(tr("Name"), QStringLiteral("name"));
    } else {
        m_sort->addItem(tr("Most popular"), QStringLiteral("popular"));
        m_sort->addItem(tr("Name"), QStringLiteral("name"));
        m_sort->addItem(tr("Most cubes"), QStringLiteral("cubes"));
    }

    toolbar->addWidget(m_search, 1);
    toolbar->addWidget(m_filter);
    if (kind == Maps) {
        m_yearFilter = new QComboBox(this);
        m_yearFilter->setObjectName("archiveMapYearFilter");
        m_yearFilter->addItem(tr("All years"), QString());
        toolbar->addWidget(m_yearFilter);
    }
    toolbar->addWidget(m_sort);
    layout->addLayout(toolbar);

    m_status->setObjectName(kind == Maps ? "archiveMapStatus" : kind == Avatars ? "archiveAvatarStatus" : "archiveModelStatus");
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->hide();
    layout->addWidget(m_status);

    m_list->setObjectName(kind == Maps ? "archiveMapsList" : kind == Avatars ? "archiveAvatarsList" : "archiveModelsList");
    m_list->setAccessibleName(kind == Maps ? tr("Archived maps") : kind == Avatars ? tr("Archived avatars") : tr("Archived models"));
    TileGrid::setup(m_list, this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_list, 1);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    auto *actions = new QHBoxLayout;
    m_selected->setObjectName(kind == Maps ? "archiveSelectedMapLabel" : kind == Avatars ? "archiveSelectedAvatarLabel" : "archiveSelectedModelLabel");
    m_selected->setTextFormat(Qt::PlainText);
    m_detail->setObjectName("selectedItemDetail");
    m_detail->setTextFormat(Qt::PlainText);
    m_addButton->setObjectName(kind == Maps ? "archiveAddMapButton" : kind == Avatars ? "archiveAddAvatarButton" : "archiveAddModelButton");
    m_addButton->setProperty("primary", true);
    m_addButton->setMinimumWidth(140);

    actions->addWidget(m_selected);
    actions->addWidget(m_detail);
    actions->addStretch();
    actions->addWidget(m_addButton);
    layout->addLayout(actions);

    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SearchDebounceMs);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        if (m_kind == Models && m_loaded && !m_search->text().trimmed().isEmpty()
            && m_loadedCardChunks < qMin(ModelSearchChunks, m_totalCardChunks) && !m_loadingChunk) {
            loadCardChunk(m_loadedCardChunks);
        }
        applyFilter();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, &ArchiveBrowser::applyFilter);
    if (m_yearFilter)
        connect(m_yearFilter, &QComboBox::currentIndexChanged, this, &ArchiveBrowser::applyFilter);
    connect(m_sort, &QComboBox::currentIndexChanged, this, &ArchiveBrowser::applyFilter);

    connect(m_list, &QListWidget::currentItemChanged, this, &ArchiveBrowser::updateActions);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ArchiveBrowser::updateActions);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ArchiveBrowser::showContextMenu);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item || m_importing) return;
        const int idx = item->data(IndexRole).toInt();
        if (idx >= 0 && idx < m_entries.size())
            requestImportEntries({idx});
    });
    connect(m_addButton, &QPushButton::clicked, this, &ArchiveBrowser::requestImportSelection);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value < m_list->verticalScrollBar()->maximum() - ScrollThreshold) return;
        if (m_shown < m_filtered.size()) {
            appendPage();
        } else if (m_kind != Maps && m_loaded && m_loadedCardChunks < m_totalCardChunks && !m_loadingChunk) {
            loadCardChunk(m_loadedCardChunks);
        }
    });

    auto *refresh = new QShortcut(QKeySequence::Refresh, this);
    refresh->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refresh, &QShortcut::activated, this, &ArchiveBrowser::reload);

    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        m_placeholder = QIcon();
        if (m_loaded) applyFilter();
    });

    updateActions();
}

void ArchiveBrowser::setEndpoints(const QUrl &archiveUrl, const QUrl &metaUrl)
{
    m_archiveUrl = archiveUrl;
    m_metaUrl = metaUrl;
}

void ArchiveBrowser::ensureLoaded()
{
    if (m_loaded || m_loading) return;
    fetchLatest();
}

void ArchiveBrowser::reload()
{
    m_loaded = false;
    m_loadingChunk = false;
    m_loadedCardChunks = 0;
    m_totalCardChunks = 0;
    m_entries.clear();
    m_mapDetails.clear();
    m_mapTitles.clear();
    m_metaRows = QJsonObject();
    m_types.clear();
    fetchLatest();
}

bool ArchiveBrowser::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && event->type() == QEvent::Resize)
        TileGrid::fit(m_list, m_kind == Maps ? TileGrid::WideTile : TileGrid::SquareTile);
    return QWidget::eventFilter(watched, event);
}

QString ArchiveBrowser::cachePrefix() const
{
    return m_kind == Maps ? QStringLiteral("maps-") : m_kind == Avatars ? QStringLiteral("avatars-") : QStringLiteral("models-");
}

void ArchiveBrowser::fetchLatest()
{
    if (m_loading) return;
    m_loading = true;
    m_status->setText(m_kind == Maps ? tr("Loading maps archive...")
        : m_kind == Avatars ? tr("Loading avatars archive...") : tr("Loading models archive..."));
    m_status->show();

    QNetworkReply *reply = m_net->get(archiveRequest(m_archiveUrl.resolved(QUrl("latest.json"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_loading = false;
            m_status->setText(tr("Could not load the archive. Press F5 to try again."));
            m_status->show();
            return;
        }
        const QJsonObject latest = QJsonDocument::fromJson(reply->readAll()).object();
        m_build = latest.value("build").toString();
        static const QRegularExpression buildName(QStringLiteral("^[A-Za-z0-9_-][A-Za-z0-9._-]{0,63}$"));
        if (!buildName.match(m_build).hasMatch()) {
            m_loading = false;
            m_status->setText(tr("Could not load the archive. Press F5 to try again."));
            m_status->show();
            return;
        }
        m_buildUrl = m_archiveUrl.resolved(QUrl("builds/" + m_build + "/"));

        if (m_kind == Maps) {
            m_indexPath = latest.value("index").toObject().value("path").toString("index.json");
            const QString imagesPath = latest.value("images").toObject().value("path").toString("../../images/");
            m_imagesUrl = m_buildUrl.resolved(QUrl(imagesPath));
            const QJsonObject details = latest.value("details").toObject();
            m_detailsPath = details.value("path").toString("details/");
            m_detailsPerFile = qMax(1, details.value("perFile").toInt(1024));
            loadMapArchive();
        } else {
            const QJsonObject cards = latest.value("cards").toObject();
            m_cardsPath = cards.value("path").toString("cards/");
            m_cardRecordSize = qMax(m_kind == Models ? 57 : 54, cards.value("record").toInt(m_kind == Models ? 57 : 54));
            m_cardsPerFile = qMax(1, cards.value("perFile").toInt(16384));
            m_totalCards = latest.value(m_kind == Models ? "models" : "avatars").toInt();
            m_totalCardChunks = qMax(1, (m_totalCards + m_cardsPerFile - 1) / m_cardsPerFile);
            m_titlesPath = latest.value("titles").toString("maps.json");
            m_imagePath = latest.value("imagePath").toString("images/");
            loadCardArchive();
        }
    });
}

void ArchiveBrowser::loadMapArchive()
{
    const QString cacheFile = "maps-" + m_build + "-index.json";
    const QByteArray cached = readCache(cacheFile);
    if (!cached.isEmpty()) {
        m_loading = false;
        m_loaded = true;
        parseMapIndex(cached);
        return;
    }

    QNetworkReply *reply = m_net->get(archiveRequest(m_buildUrl.resolved(QUrl(m_indexPath))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheFile]() {
        reply->deleteLater();
        m_loading = false;
        if (reply->error() != QNetworkReply::NoError) {
            m_status->setText(tr("Could not load the archive. Press F5 to try again."));
            m_status->show();
            return;
        }
        const QByteArray data = reply->readAll();
        pruneCache("maps-", "maps-" + m_build + "-");
        writeCache(cacheFile, data);
        m_loaded = true;
        parseMapIndex(data);
    });
}

void ArchiveBrowser::loadCardArchive()
{
    struct State {
        QByteArray cards0;
        QByteArray titles;
        QByteArray meta;
        int remaining = 0;
        bool cardsFailed = false;
    };
    auto state = std::make_shared<State>();

    const QString basePrefix = cachePrefix();
    const QString prefix = basePrefix + m_build + "-";
    const bool hasMeta = m_metaUrl.isValid() && !m_metaUrl.isEmpty();

    auto checkDone = [this, state, basePrefix, prefix]() {
        if (state->remaining > 0) return;
        m_loading = false;
        if (state->cardsFailed) {
            m_status->setText(tr("Could not load the archive. Press F5 to try again."));
            m_status->show();
            return;
        }
        pruneCache(basePrefix, prefix);

        m_mapTitles.clear();
        if (!state->titles.isEmpty()) {
            const QJsonArray array = QJsonDocument::fromJson(state->titles).array();
            m_mapTitles.reserve(array.size());
            for (const QJsonValue &val : array)
                m_mapTitles.append(val.toString().trimmed());
        }

        m_metaRows = QJsonObject();
        if (!state->meta.isEmpty()) {
            const QJsonObject metaDoc = QJsonDocument::fromJson(state->meta).object();
            if (metaDoc.value("build").toString() == m_build)
                m_metaRows = metaDoc.value("rows").toObject();
        }

        m_entries.clear();
        m_types.clear();
        m_loadedCardChunks = 0;
        parseCardChunk(0, state->cards0);
        m_loadedCardChunks = 1;
        m_loaded = true;
        refreshTypeFilter();
        applyFilter();
    };

    const QString cardsFile = prefix + "cards-000.bin";
    state->cards0 = readCache(cardsFile);
    if (state->cards0.isEmpty()) {
        state->remaining++;
        const QUrl url = m_buildUrl.resolved(QUrl(m_cardsPath + "000.bin"));
        QNetworkReply *reply = m_net->get(archiveRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, state, cardsFile, checkDone]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                state->cards0 = reply->readAll();
                writeCache(cardsFile, state->cards0);
            } else {
                state->cardsFailed = true;
            }
            state->remaining--;
            checkDone();
        });
    }

    const QString titlesCache = prefix + "maps.json";
    state->titles = readCache(titlesCache);
    if (state->titles.isEmpty() && !m_titlesPath.isEmpty()) {
        state->remaining++;
        QNetworkReply *reply = m_net->get(archiveRequest(m_buildUrl.resolved(QUrl(m_titlesPath))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, state, titlesCache, checkDone]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                state->titles = reply->readAll();
                writeCache(titlesCache, state->titles);
            }
            state->remaining--;
            checkDone();
        });
    }

    if (hasMeta) {
        const QString metaCache = prefix + "meta.json";
        state->meta = readCache(metaCache);
        if (state->meta.isEmpty()) {
            state->remaining++;
            QNetworkReply *reply = m_net->get(archiveRequest(m_metaUrl));
            connect(reply, &QNetworkReply::finished, this, [this, reply, state, metaCache, checkDone]() {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    const QByteArray raw = reply->readAll();
                    if (QJsonDocument::fromJson(raw).object().value("build").toString() == m_build) {
                        state->meta = raw;
                        writeCache(metaCache, raw);
                    }
                }
                state->remaining--;
                checkDone();
            });
        }
    }

    checkDone();
}

void ArchiveBrowser::loadCardChunk(int chunkIndex, std::function<void()> done)
{
    if (m_loadingChunk || chunkIndex < m_loadedCardChunks || chunkIndex >= m_totalCardChunks) {
        if (done) done();
        return;
    }
    m_loadingChunk = true;

    const QString fileName = cachePrefix() + m_build + "-cards-" + hexChunk(chunkIndex) + ".bin";
    auto finishChunk = [this, chunkIndex, done](const QByteArray &data) {
        m_loadingChunk = false;
        if (!data.isEmpty() && chunkIndex == m_loadedCardChunks) {
            parseCardChunk(chunkIndex, data);
            m_loadedCardChunks = chunkIndex + 1;
            refreshTypeFilter();
            applyFilter();
        }
        if (done) done();
        // If searching models, continue loading up to ModelSearchChunks
        if (m_kind == Models && !m_search->text().trimmed().isEmpty()
            && m_loadedCardChunks < qMin(ModelSearchChunks, m_totalCardChunks)) {
            loadCardChunk(m_loadedCardChunks);
        }
    };

    const QByteArray cached = readCache(fileName);
    if (!cached.isEmpty()) {
        finishChunk(cached);
        return;
    }

    const QUrl url = m_buildUrl.resolved(QUrl(m_cardsPath + hexChunk(chunkIndex) + ".bin"));
    QNetworkReply *reply = m_net->get(archiveRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, finishChunk]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finishChunk({});
            return;
        }
        const QByteArray data = reply->readAll();
        writeCache(fileName, data);
        finishChunk(data);
    });
}

void ArchiveBrowser::parseMapIndex(const QByteArray &data)
{
    m_entries.clear();
    const QJsonObject index = QJsonDocument::fromJson(data).object();
    const QJsonArray fields = index.value("fields").toArray();
    auto fieldIndex = [&](const QString &name) {
        for (int i = 0; i < fields.size(); ++i)
            if (fields[i].toString() == name) return i;
        return -1;
    };

    const int iId = fieldIndex("id");
    const int iSite = fieldIndex("site");
    const int iName = fieldIndex("name");
    const int iAuthor = fieldIndex("author");
    const int iPlays = fieldIndex("plays");
    const int iLikes = fieldIndex("likes");
    const int iPublished = fieldIndex("published");
    const int iFormat = fieldIndex("format");
    const int iSize = fieldIndex("size");
    const int iImage = fieldIndex("image");

    const QJsonArray rows = index.value("rows").toArray();
    m_entries.reserve(rows.size());
    const QString untitled = tr("Untitled map");
    QSet<QString> years;

    for (int pos = 0; pos < rows.size(); ++pos) {
        const QJsonArray row = rows[pos].toArray();
        Entry e;
        e.position = pos;
        const QString idStr = iId >= 0 ? row.at(iId).toString() : QString();
        e.id = idStr.toInt();
        e.site = iSite >= 0 ? row.at(iSite).toString() : QString();
        e.name = iName >= 0 ? row.at(iName).toString().trimmed() : QString();
        if (e.name.isEmpty()) e.name = untitled;
        e.author = iAuthor >= 0 ? row.at(iAuthor).toString().trimmed() : QString();
        e.plays = iPlays >= 0 ? row.at(iPlays).toVariant().toLongLong() : 0;
        e.likes = iLikes >= 0 ? row.at(iLikes).toVariant().toLongLong() : 0;
        e.published = iPublished >= 0 ? row.at(iPublished).toString() : QString();
        if (e.published.size() >= 4) {
            bool ok = false;
            const int yr = e.published.left(4).toInt(&ok);
            if (ok && yr >= 2011 && yr <= 2100) years.insert(e.published.left(4));
        }
        e.format = iFormat >= 0 ? row.at(iFormat).toString() : QString();
        e.size = iSize >= 0 ? row.at(iSize).toVariant().toLongLong() : 0;
        e.image = iImage >= 0 ? row.at(iImage).toString() : QString();
        e.hasImage = !e.image.isEmpty();
        e.searchText = (e.name + " " + e.author + " " + idStr).toLower();
        m_entries.append(std::move(e));
    }

    if (m_yearFilter) {
        const QString previous = m_yearFilter->currentData().toString();
        QSignalBlocker blocker(m_yearFilter);
        m_yearFilter->clear();
        m_yearFilter->addItem(tr("All years"), QString());
        QStringList sortedYears(years.cbegin(), years.cend());
        std::sort(sortedYears.begin(), sortedYears.end(), std::greater<QString>());
        for (const QString &yr : sortedYears)
            m_yearFilter->addItem(yr, yr);
        const int idx = m_yearFilter->findData(previous);
        m_yearFilter->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    applyFilter();
}

void ArchiveBrowser::parseCardChunk(int chunkIndex, const QByteArray &cards)
{
    const int count = cards.size() / m_cardRecordSize;
    if (count <= 0) return;

    m_entries.reserve(m_entries.size() + count);
    const auto *raw = reinterpret_cast<const uchar *>(cards.constData());
    const int baseId = chunkIndex * m_cardsPerFile;

    for (int i = 0; i < count; ++i) {
        const int id = baseId + i + 1;
        const uchar *rec = raw + i * m_cardRecordSize;

        QString name;
        QString type;
        QString desc;
        QString tags;
        const QJsonValue metaVal = m_metaRows.value(QString::number(id));
        if (metaVal.isArray()) {
            const QJsonArray info = metaVal.toArray();
            const QString rating = info.at(4).toString().trimmed();
            if (!rating.isEmpty()) continue;
            name = info.at(0).toString().trimmed();
            type = info.at(1).toString().trimmed().toLower();
            desc = info.at(2).toString().trimmed();
            tags = info.at(5).toString().trimmed();
        }

        Entry e;
        e.id = id;
        e.position = baseId + i;
        e.sha256 = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(rec), 32).toHex());
        e.maps = qFromLittleEndian<quint32>(rec + 32);
        e.cubes = qFromLittleEndian<quint32>(rec + 36);

        quint16 mapIdx = 0;
        if (m_kind == Models) {
            e.scale = readLittleFloat(rec + 40);
            e.hasImage = rec[44] != 0;
            e.pack = qFromLittleEndian<quint16>(rec + 45);
            e.offset = qFromLittleEndian<quint32>(rec + 47);
            e.length = qFromLittleEndian<quint32>(rec + 51);
            mapIdx = qFromLittleEndian<quint16>(rec + 55);
        } else {
            e.sources = rec[40];
            e.hasImage = rec[41] != 0;
            e.pack = qFromLittleEndian<quint16>(rec + 42);
            e.offset = qFromLittleEndian<quint32>(rec + 44);
            e.length = qFromLittleEndian<quint32>(rec + 48);
            mapIdx = qFromLittleEndian<quint16>(rec + 52);
        }

        const QString mapTitle = mapIdx < m_mapTitles.size() ? m_mapTitles.at(mapIdx) : QString();
        if (name.isEmpty()) {
            name = !mapTitle.isEmpty() ? mapTitle
                : (m_kind == Models ? tr("Model %1").arg(id) : tr("Avatar %1").arg(id));
        }
        e.name = name;
        e.type = type;
        e.author = mapTitle;
        if (!type.isEmpty()) m_types.insert(type);

        e.searchText = (name + " " + type + " " + desc + " " + tags + " " + mapTitle + " " + QString::number(id)).toLower();
        m_entries.append(std::move(e));
    }
}

void ArchiveBrowser::refreshTypeFilter()
{
    if (m_kind == Maps) return;
    const QString previous = m_filter->currentData().toString();
    QSignalBlocker blocker(m_filter);
    m_filter->clear();
    m_filter->addItem(tr("All types"), QString());
    QStringList sortedTypes(m_types.cbegin(), m_types.cend());
    sortedTypes.sort();
    for (const QString &t : sortedTypes)
        m_filter->addItem(capitalize(t), t);
    const int idx = m_filter->findData(previous);
    m_filter->setCurrentIndex(idx >= 0 ? idx : 0);
}

void ArchiveBrowser::applyFilter()
{
    const QString query = m_search->text().trimmed().toLower();
    const QStringList words = query.split(' ', Qt::SkipEmptyParts);
    const QString filterVal = m_filter->currentData().toString();
    const QString yearVal = m_yearFilter ? m_yearFilter->currentData().toString() : QString();
    const QString sortKey = m_sort->currentData().toString();

    m_filtered.clear();
    m_filtered.reserve(m_entries.size());

    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries[i];
        if (!filterVal.isEmpty()) {
            if (m_kind == Maps && e.site != filterVal) continue;
            if (m_kind != Maps && e.type != filterVal) continue;
        }
        if (!yearVal.isEmpty() && e.published.left(4) != yearVal)
            continue;
        bool match = true;
        for (const QString &word : words) {
            if (!e.searchText.contains(word)) {
                match = false;
                break;
            }
        }
        if (match) m_filtered.append(i);
    }

    std::sort(m_filtered.begin(), m_filtered.end(), [&](int a, int b) {
        const Entry &ea = m_entries[a];
        const Entry &eb = m_entries[b];
        if (m_kind == Maps) {
            if (sortKey == "likes") return ea.likes != eb.likes ? ea.likes > eb.likes : ea.plays > eb.plays;
            if (sortKey == "new") return ea.published != eb.published ? ea.published > eb.published : ea.plays > eb.plays;
            if (sortKey == "name") return ea.name.compare(eb.name, Qt::CaseInsensitive) < 0;
            return ea.plays != eb.plays ? ea.plays > eb.plays : ea.likes > eb.likes;
        } else {
            if (sortKey == "name") return ea.name.compare(eb.name, Qt::CaseInsensitive) < 0;
            if (sortKey == "cubes") return ea.cubes != eb.cubes ? ea.cubes > eb.cubes : ea.maps > eb.maps;
            return ea.maps != eb.maps ? ea.maps > eb.maps : ea.id < eb.id;
        }
    });

    m_generation++;
    m_thumbnailQueue.clear();
    m_thumbnailQueued.clear();
    m_shown = 0;

    {
        QSignalBlocker blocker(m_list);
        m_list->clear();
    }

    if (m_placeholder.isNull()) {
        const QSize imgSize = m_kind == Maps ? TileGrid::WideImage : TileGrid::SquareImage;
        m_placeholder = TileGrid::icon(TileGrid::placeholder(imgSize, palette().color(QPalette::Button)));
    }

    if (m_filtered.isEmpty()) {
        m_status->setText(m_entries.isEmpty()
            ? tr("Could not load the archive. Press F5 to try again.")
            : (m_kind == Maps ? tr("No archived maps match your search.")
                : m_kind == Avatars ? tr("No archived avatars match your search.")
                : tr("No archived models match your search.")));
        m_status->show();
    } else {
        m_status->hide();
        appendPage();
        if (m_list->count() > 0)
            m_list->setCurrentRow(0);
    }

    TileGrid::fit(m_list, m_kind == Maps ? TileGrid::WideTile : TileGrid::SquareTile);
    updateActions();
}

void ArchiveBrowser::appendPage()
{
    const int end = qMin(m_shown + PageSize, int(m_filtered.size()));
    if (end <= m_shown) return;

    QSignalBlocker blocker(m_list);
    for (int i = m_shown; i < end; ++i) {
        const int entryIdx = m_filtered[i];
        const Entry &e = m_entries[entryIdx];
        const QUrl thumbUrl = thumbnailUrlFor(e);
        const QString thumbKey = thumbUrl.toString();

        const QIcon icon = !thumbKey.isEmpty() && m_thumbnailCache.contains(thumbKey)
            ? m_thumbnailCache.value(thumbKey) : m_placeholder;

        auto *item = new QListWidgetItem(icon, e.name, m_list);
        item->setData(IndexRole, entryIdx);
        item->setData(CenteredDelegate::SubtitleRole, subtitleFor(e));
        if (!thumbKey.isEmpty()) {
            item->setData(ThumbnailRole, thumbKey);
            if (!m_thumbnailCache.contains(thumbKey))
                loadThumbnailFor(item);
        }
    }
    m_shown = end;
    fetchNextThumbnail();
}

void ArchiveBrowser::loadThumbnailFor(QListWidgetItem *item)
{
    const QString url = item->data(ThumbnailRole).toString();
    if (url.isEmpty() || m_thumbnailQueued.contains(url)) return;
    m_thumbnailQueued.insert(url);
    m_thumbnailQueue.enqueue({m_list->row(item), url});
}

void ArchiveBrowser::fetchNextThumbnail()
{
    while (m_activeThumbnails < MaxConcurrentThumbnails && !m_thumbnailQueue.isEmpty()) {
        const auto [row, urlStr] = m_thumbnailQueue.dequeue();
        if (m_thumbnailCache.contains(urlStr)) {
            if (row >= 0 && row < m_list->count()) {
                QListWidgetItem *item = m_list->item(row);
                if (item && item->data(ThumbnailRole).toString() == urlStr)
                    item->setIcon(m_thumbnailCache.value(urlStr));
            }
            continue;
        }

        m_activeThumbnails++;
        const int gen = m_generation;
        QNetworkReply *reply = m_thumbNet->get(archiveRequest(QUrl(urlStr), ThumbnailTimeoutMs));
        connect(reply, &QNetworkReply::finished, this, [this, reply, row, urlStr, gen]() {
            reply->deleteLater();
            m_activeThumbnails = qMax(0, m_activeThumbnails - 1);
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap pixmap;
                if (pixmap.loadFromData(reply->readAll())) {
                    const QSize imgSize = m_kind == Maps ? TileGrid::WideImage : TileGrid::SquareImage;
                    const QIcon icon = TileGrid::icon(TileGrid::cover(pixmap, imgSize));
                    if (m_thumbnailCache.size() >= MaxCachedThumbnails)
                        m_thumbnailCache.clear();
                    m_thumbnailCache.insert(urlStr, icon);
                    if (gen == m_generation && row >= 0 && row < m_list->count()) {
                        QListWidgetItem *item = m_list->item(row);
                        if (item && item->data(ThumbnailRole).toString() == urlStr)
                            item->setIcon(icon);
                    }
                }
            }
            fetchNextThumbnail();
        });
    }
}

QUrl ArchiveBrowser::thumbnailUrlFor(const Entry &entry) const
{
    if (!entry.hasImage) return {};
    if (m_kind == Maps)
        return m_imagesUrl.resolved(QUrl(entry.image + ".jpg"));
    return m_buildUrl.resolved(QUrl(m_imagePath + entry.sha256 + ".png"));
}

QString ArchiveBrowser::subtitleFor(const Entry &entry) const
{
    if (m_kind == Maps) {
        QStringList parts;
        if (!entry.author.isEmpty()) parts << entry.author;
        if (entry.plays > 0) parts << tr("%1 plays").arg(formatCount(entry.plays));
        else if (entry.size > 0) parts << formatSize(entry.size);
        if (entry.published.size() >= 4) parts << entry.published.left(4);
        return parts.join(QString::fromUtf8(" \u00b7 "));
    }
    QStringList parts;
    if (!entry.type.isEmpty()) parts << capitalize(entry.type);
    if (entry.maps > 0) parts << tr("%n map(s)", nullptr, int(entry.maps));
    else if (entry.cubes > 0) parts << tr("%1 cubes").arg(formatCount(entry.cubes));
    return parts.join(QString::fromUtf8(" \u00b7 "));
}

QString ArchiveBrowser::detailFor(const Entry &entry) const
{
    if (m_kind == Maps) {
        QStringList parts;
        if (!entry.author.isEmpty()) parts << tr("by %1").arg(entry.author);
        if (entry.size > 0) parts << formatSize(entry.size);
        if (!entry.published.isEmpty()) parts << entry.published.left(4);
        return parts.join(QString::fromUtf8(" \u00b7 "));
    }
    QStringList parts;
    if (!entry.type.isEmpty()) parts << capitalize(entry.type);
    if (m_kind == Models && entry.scale > 0.0f) parts << tr("scale %1").arg(QString::number(entry.scale));
    if (entry.cubes > 0) parts << tr("%1 cubes").arg(formatCount(entry.cubes));
    return parts.join(QString::fromUtf8(" \u00b7 "));
}

void ArchiveBrowser::updateActions()
{
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    const int count = int(selected.size());
    if (count == 0) {
        m_selected->setText(m_kind == Maps ? tr("No map selected")
            : m_kind == Avatars ? tr("No avatar selected") : tr("No model selected"));
        m_detail->clear();
        m_addButton->setText(m_kind == Maps ? tr("Add to my worlds")
            : m_kind == Avatars ? tr("Add to my avatars") : tr("Add to my inventory"));
        m_addButton->setEnabled(false);
        return;
    }

    if (count == 1) {
        const int entryIdx = selected.first()->data(IndexRole).toInt();
        if (entryIdx < 0 || entryIdx >= m_entries.size()) {
            m_addButton->setEnabled(false);
            return;
        }
        const Entry &e = m_entries[entryIdx];
        m_selected->setText(e.name);
        m_detail->setText(detailFor(e));
        m_addButton->setText(m_kind == Maps ? tr("Add to my worlds")
            : m_kind == Avatars ? tr("Add to my avatars") : tr("Add to my inventory"));
        m_addButton->setEnabled(!m_importing);
        return;
    }

    m_selected->setText(tr("%n selected", nullptr, count));
    m_detail->clear();
    m_addButton->setText(m_kind == Maps ? tr("Add %n map(s)", nullptr, count)
        : m_kind == Avatars ? tr("Add %n avatar(s)", nullptr, count)
        : tr("Add %n model(s)", nullptr, count));
    m_addButton->setEnabled(!m_importing);
}

void ArchiveBrowser::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item) {
        QMenu menu(this);
        menu.addAction(tr("Refresh"), this, &ArchiveBrowser::reload);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }
    if (!item->isSelected()) {
        m_list->clearSelection();
        m_list->setCurrentItem(item);
        item->setSelected(true);
    }

    const QList<QListWidgetItem *> sel = m_list->selectedItems();
    const int count = int(sel.size());
    const bool single = count == 1;
    const int entryIdx = item->data(IndexRole).toInt();
    const bool validEntry = entryIdx >= 0 && entryIdx < m_entries.size();

    QMenu menu(this);
    const QString addText = single
        ? (m_kind == Maps ? tr("Add to my worlds") : m_kind == Avatars ? tr("Add to my avatars") : tr("Add to my inventory"))
        : (m_kind == Maps ? tr("Add %n map(s)", nullptr, count)
            : m_kind == Avatars ? tr("Add %n avatar(s)", nullptr, count)
            : tr("Add %n model(s)", nullptr, count));
    QAction *add = menu.addAction(addText);
    add->setEnabled(!m_importing);

    QAction *downloadClient = nullptr;
    QAction *copyName = nullptr;
    QAction *byAuthor = nullptr;
    QAction *openWeb = nullptr;
    if (single && validEntry) {
        const Entry &e = m_entries[entryIdx];
        if (m_kind == Maps && e.published.size() >= 4)
            downloadClient = menu.addAction(tr("Download matching client (%1)...").arg(e.published.left(4)));
        menu.addSeparator();
        copyName = menu.addAction(tr("Copy name"));
        if (m_kind == Maps && !e.author.isEmpty())
            byAuthor = menu.addAction(tr("More by %1").arg(e.author));
        openWeb = menu.addAction(tr("Open on openkogama.org"));
    }

    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == add) {
        requestImportSelection();
    } else if (chosen == downloadClient && validEntry) {
        const Entry &e = m_entries[entryIdx];
        emit clientForYearRequested(e.published, e.name);
    } else if (chosen == copyName && validEntry) {
        QGuiApplication::clipboard()->setText(m_entries[entryIdx].name);
    } else if (chosen == byAuthor && validEntry) {
        m_search->setText(m_entries[entryIdx].author);
    } else if (chosen == openWeb && validEntry) {
        const Entry &e = m_entries[entryIdx];
        QString url;
        if (m_kind == Maps)
            url = QStringLiteral("https://openkogama.org/maps/?q=") + QString::fromLatin1(QUrl::toPercentEncoding(e.id > 0 ? QString::number(e.id) : e.name));
        else if (m_kind == Avatars)
            url = QStringLiteral("https://openkogama.org/avatars/?q=") + QString::fromLatin1(QUrl::toPercentEncoding(e.name));
        else
            url = QStringLiteral("https://openkogama.org/models/?q=") + QString::fromLatin1(QUrl::toPercentEncoding(e.name));
        QDesktopServices::openUrl(QUrl(url));
    }
}

void ArchiveBrowser::requestImportSelection()
{
    if (m_importing) return;
    QVector<int> indices;
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    indices.reserve(selected.size());
    for (QListWidgetItem *item : selected) {
        const int idx = item->data(IndexRole).toInt();
        if (idx >= 0 && idx < m_entries.size())
            indices.append(idx);
    }
    if (indices.isEmpty()) {
        if (QListWidgetItem *current = m_list->currentItem()) {
            const int idx = current->data(IndexRole).toInt();
            if (idx >= 0 && idx < m_entries.size())
                indices.append(idx);
        }
    }
    if (!indices.isEmpty())
        requestImportEntries(indices);
}

void ArchiveBrowser::requestImportEntries(const QVector<int> &entryIndices)
{
    if (entryIndices.isEmpty()) return;

    if (m_kind == Maps) {
        QVector<Entry> maps;
        maps.reserve(entryIndices.size());
        for (int idx : entryIndices) maps.append(m_entries[idx]);
        requestMapDetails(maps);
        return;
    }

    for (int idx : entryIndices) {
        const Entry &e = m_entries[idx];
        ImportRequest req;
        req.kind = m_kind == Models ? QStringLiteral("model") : QStringLiteral("avatar");
        req.name = e.name;
        req.sha256 = e.sha256;
        req.offset = e.offset;
        req.length = e.length;
        if (m_kind == Models && e.scale > 0.0f)
            req.scale = QString::number(e.scale);
        req.urls = {m_buildUrl.resolved(QUrl("packs/" + hexChunk(e.pack) + ".bin"))};
        emit importRequested(req, thumbnailUrlFor(e));
    }
}

void ArchiveBrowser::requestMapDetails(const QVector<Entry> &entries)
{
    QSet<int> missingChunks;
    for (const Entry &e : entries) {
        const int chunk = e.position / m_detailsPerFile;
        if (!m_mapDetails.contains(chunk))
            missingChunks.insert(chunk);
    }

    auto emitAll = [this, entries]() {
        int missingLinks = 0;
        for (const Entry &e : entries) {
            const int chunk = e.position / m_detailsPerFile;
            const int inChunk = e.position % m_detailsPerFile;
            if (!m_mapDetails.contains(chunk)) {
                missingLinks++;
                continue;
            }
            const QJsonArray item = m_mapDetails.value(chunk).at(inChunk).toArray();
            const QJsonArray versions = item.at(3).toArray();
            if (versions.isEmpty()) {
                missingLinks++;
                continue;
            }
            const QJsonArray latestVersion = versions.first().toArray();
            ImportRequest req;
            req.kind = QStringLiteral("map");
            req.name = e.name;
            req.published = e.published;
            req.sha256 = latestVersion.at(0).toString().trimmed().toLower();
            static const QRegularExpression sha(QStringLiteral("^[0-9a-f]{64}$"));
            if (!sha.match(req.sha256).hasMatch()) {
                missingLinks++;
                continue;
            }
            for (const QJsonValue &val : latestVersion.at(5).toArray()) {
                const QUrl u(val.toString());
                if (u.isValid() && u.scheme() == "https")
                    req.urls.append(u);
            }
            if (req.urls.isEmpty()) {
                missingLinks++;
                continue;
            }
            emit importRequested(req, thumbnailUrlFor(e));
        }
        if (missingLinks > 0 && entries.size() == 1)
            QMessageBox::warning(this, tr("Import"), tr("No download links are available for this map."));
    };

    if (missingChunks.isEmpty()) {
        emitAll();
        return;
    }

    m_importing = true;
    m_addButton->setEnabled(false);

    struct BatchState {
        int remaining = 0;
        QString lastError;
    };
    auto state = std::make_shared<BatchState>();
    state->remaining = int(missingChunks.size());

    for (int chunk : std::as_const(missingChunks)) {
        const QUrl detailUrl = m_buildUrl.resolved(QUrl(m_detailsPath + hexChunk(chunk) + ".json"));
        QNetworkReply *reply = m_net->get(archiveRequest(detailUrl));
        connect(reply, &QNetworkReply::finished, this, [this, reply, chunk, state, emitAll]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonArray rows = QJsonDocument::fromJson(reply->readAll()).array();
                if (!rows.isEmpty()) m_mapDetails.insert(chunk, rows);
            } else {
                state->lastError = reply->errorString();
            }
            state->remaining--;
            if (state->remaining == 0) {
                m_importing = false;
                updateActions();
                if (!state->lastError.isEmpty() && m_mapDetails.isEmpty()) {
                    QMessageBox::warning(this, tr("Import"), tr("Could not fetch map details: %1").arg(state->lastError));
                    return;
                }
                emitAll();
            }
        });
    }
}

QByteArray ArchiveBrowser::readCache(const QString &fileName) const
{
    QFile file(cacheDir() + "/" + fileName);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void ArchiveBrowser::writeCache(const QString &fileName, const QByteArray &data) const
{
    if (data.isEmpty()) return;
    QDir().mkpath(cacheDir());
    QSaveFile file(cacheDir() + "/" + fileName);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(data);
    file.commit();
}

void ArchiveBrowser::pruneCache(const QString &prefix, const QString &keepPrefix) const
{
    QDir dir(cacheDir());
    if (!dir.exists()) return;
    for (const QString &entry : dir.entryList({prefix + "*"}, QDir::Files)) {
        if (!entry.startsWith(keepPrefix))
            dir.remove(entry);
    }
}
