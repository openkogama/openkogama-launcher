#include "downloadmanager.h"
#include "contentimporter.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {

constexpr int SettleDelayMs = 350;
constexpr int PopupWidth = 370;
constexpr int PopupMaxHeight = 420;

QString kindLabel(const QString &kind)
{
    if (kind == "map") return DownloadManager::tr("Map");
    if (kind == "avatar") return DownloadManager::tr("Avatar");
    return DownloadManager::tr("Model");
}

QString entryDisplayName(const DownloadEntry &entry)
{
    if (!entry.request.name.isEmpty()) return entry.request.name;
    if (entry.request.kind == "map") return DownloadManager::tr("Map");
    if (entry.request.kind == "avatar") return DownloadManager::tr("Avatar");
    return DownloadManager::tr("Model");
}

}

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
    , m_settleTimer(new QTimer(this))
{
    m_settleTimer->setSingleShot(true);
    m_settleTimer->setInterval(SettleDelayMs);
    connect(m_settleTimer, &QTimer::timeout, this, &DownloadManager::flushSettled);
}

int DownloadManager::enqueue(const ImportRequest &request, const QUrl &thumbnailUrl, const QString &clientVersion)
{
    DownloadEntry entry;
    entry.jobId = m_nextId++;
    entry.request = request;
    entry.thumbnailUrl = thumbnailUrl;
    entry.clientVersion = clientVersion;
    entry.state = DownloadEntry::Queued;
    m_entries.append(entry);

    emit jobAdded(entry.jobId);
    emit changed();
    pump();
    return entry.jobId;
}

void DownloadManager::cancel(int jobId)
{
    const int idx = indexOf(jobId);
    if (idx < 0) return;
    DownloadEntry &entry = m_entries[idx];
    if (entry.state == DownloadEntry::Queued) {
        entry.state = DownloadEntry::Cancelled;
        entry.error.clear();
        emit jobUpdated(jobId);
        emit changed();
        return;
    }
    if (entry.state == DownloadEntry::Active) {
        entry.state = DownloadEntry::Cancelled;
        entry.error.clear();
        if (entry.importer) entry.importer->cancel();
        emit jobUpdated(jobId);
        emit changed();
    }
}

void DownloadManager::cancelAll()
{
    QVector<int> ids;
    for (const DownloadEntry &entry : std::as_const(m_entries))
        if (entry.state == DownloadEntry::Queued || entry.state == DownloadEntry::Active)
            ids.append(entry.jobId);
    for (int id : ids)
        cancel(id);
}

void DownloadManager::retry(int jobId)
{
    const int idx = indexOf(jobId);
    if (idx < 0) return;
    DownloadEntry &entry = m_entries[idx];
    if (entry.state != DownloadEntry::Failed && entry.state != DownloadEntry::Cancelled) return;
    entry.state = DownloadEntry::Queued;
    entry.percent = 0;
    entry.error.clear();
    entry.importedId = 0;
    emit jobUpdated(jobId);
    emit changed();
    pump();
}

void DownloadManager::clearFinished()
{
    const qsizetype before = m_entries.size();
    m_entries.removeIf([](const DownloadEntry &e) {
        return e.state == DownloadEntry::Completed || e.state == DownloadEntry::Failed || e.state == DownloadEntry::Cancelled;
    });
    if (m_entries.size() != before) {
        emit listReset();
        emit changed();
    }
}

int DownloadManager::activeCount() const
{
    int count = 0;
    for (const DownloadEntry &entry : m_entries)
        if (entry.state == DownloadEntry::Queued || entry.state == DownloadEntry::Active)
            count++;
    return count;
}

int DownloadManager::runningCount() const
{
    return m_running;
}

int DownloadManager::queuedCount() const
{
    int count = 0;
    for (const DownloadEntry &entry : m_entries)
        if (entry.state == DownloadEntry::Queued)
            count++;
    return count;
}

const DownloadEntry *DownloadManager::entryById(int jobId) const
{
    const int idx = indexOf(jobId);
    return idx >= 0 ? &m_entries.at(idx) : nullptr;
}

int DownloadManager::indexOf(int jobId) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].jobId == jobId) return i;
    return -1;
}

void DownloadManager::pump()
{
    while (m_running < MaxConcurrent) {
        int nextIdx = -1;
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].state == DownloadEntry::Queued) {
                nextIdx = i;
                break;
            }
        }
        if (nextIdx < 0) break;
        startJob(nextIdx);
    }
}

void DownloadManager::startJob(int index)
{
    const int jobId = m_entries[index].jobId;
    m_entries[index].state = DownloadEntry::Active;
    m_entries[index].percent = 0;
    m_entries[index].error.clear();
    m_running++;

    auto *importer = new ContentImporter(m_entries[index].clientVersion, this);
    m_entries[index].importer = importer;

    connect(importer, &ContentImporter::progress, this, [this, jobId](qint64 done, qint64 total) {
        const int idx = indexOf(jobId);
        if (idx < 0 || total <= 0) return;
        const int pct = int(qBound(qint64(0), done * 100 / total, qint64(100)));
        if (m_entries[idx].percent != pct) {
            m_entries[idx].percent = pct;
            emit jobUpdated(jobId);
        }
    });

    connect(importer, &ContentImporter::finished, this, [this, importer, jobId](bool ok, const QString &error, int id) {
        importer->deleteLater();
        m_running = qMax(0, m_running - 1);

        const int idx = indexOf(jobId);
        if (idx >= 0) {
            DownloadEntry &entry = m_entries[idx];
            entry.importer = nullptr;
            if (entry.state != DownloadEntry::Cancelled) {
                if (ok) {
                    entry.state = DownloadEntry::Completed;
                    entry.percent = 100;
                    entry.importedId = id;
                    entry.error.clear();
                    emit jobCompleted(entry.request.kind, id, entry.thumbnailUrl);
                    scheduleSettle(entry.request.kind, id);
                } else {
                    entry.state = DownloadEntry::Failed;
                    entry.error = error;
                }
            }
            emit jobUpdated(jobId);
        }
        emit changed();
        pump();
    });

    emit jobUpdated(jobId);
    emit changed();
    importer->start(m_entries[index].request);
}

void DownloadManager::scheduleSettle(const QString &kind, int importedId)
{
    m_pendingSettle.insert(kind, importedId);
    bool moreOfKind = false;
    for (const DownloadEntry &e : std::as_const(m_entries)) {
        if (e.request.kind == kind && (e.state == DownloadEntry::Queued || e.state == DownloadEntry::Active)) {
            moreOfKind = true;
            break;
        }
    }
    if (!moreOfKind) {
        m_settleTimer->stop();
        flushSettled();
    } else if (!m_settleTimer->isActive()) {
        m_settleTimer->start();
    }
}

void DownloadManager::flushSettled()
{
    const QHash<QString, int> pending = std::exchange(m_pendingSettle, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit batchSettled(it.key(), it.value());
}

// ---------------------------------------------------------------------------
// DownloadsPopup
// ---------------------------------------------------------------------------

DownloadsPopup::DownloadsPopup(DownloadManager *manager, QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_manager(manager)
    , m_title(new QLabel(tr("Downloads"), this))
    , m_clearButton(new QPushButton(tr("Clear"), this))
    , m_cancelAllButton(new QPushButton(tr("Cancel all"), this))
    , m_empty(new QLabel(tr("No downloads yet."), this))
    , m_scroll(new QScrollArea(this))
    , m_listWidget(new QWidget(m_scroll))
    , m_listLayout(new QVBoxLayout(m_listWidget))
{
    setObjectName("downloadsPopup");
    setFrameShape(QFrame::StyledPanel);
    setFixedWidth(PopupWidth);
    setMaximumHeight(PopupMaxHeight);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *header = new QHBoxLayout;
    header->setSpacing(6);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_clearButton->setFlat(true);
    m_cancelAllButton->setFlat(true);
    m_clearButton->setObjectName("downloadsClearButton");
    m_cancelAllButton->setObjectName("downloadsCancelAllButton");
    header->addWidget(m_title);
    header->addStretch();
    header->addWidget(m_cancelAllButton);
    header->addWidget(m_clearButton);
    root->addLayout(header);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    root->addWidget(line);

    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setObjectName("selectedItemDetail");
    m_empty->setContentsMargins(0, 16, 0, 16);
    root->addWidget(m_empty);

    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch();

    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(m_listWidget);
    root->addWidget(m_scroll, 1);

    connect(m_clearButton, &QPushButton::clicked, m_manager, &DownloadManager::clearFinished);
    connect(m_cancelAllButton, &QPushButton::clicked, m_manager, &DownloadManager::cancelAll);
    connect(m_manager, &DownloadManager::jobAdded, this, [this](int jobId) {
        if (const DownloadEntry *e = m_manager->entryById(jobId)) addRow(*e);
        updateHeader();
    });
    connect(m_manager, &DownloadManager::jobUpdated, this, [this](int jobId) {
        updateRow(jobId);
        updateHeader();
    });
    connect(m_manager, &DownloadManager::listReset, this, &DownloadsPopup::rebuild);

    rebuild();
}

void DownloadsPopup::showBelow(QWidget *anchor)
{
    if (!anchor) return;
    adjustSize();
    const int contentHeight = qMin(PopupMaxHeight, qMax(140, m_listWidget->sizeHint().height() + 72));
    resize(PopupWidth, contentHeight);

    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 4));
    if (QScreen *screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left() + 4, pos.x(), avail.right() - width() - 4));
        pos.setY(qBound(avail.top() + 4, pos.y(), avail.bottom() - height() - 4));
    }
    move(pos);
    show();
    raise();
}

void DownloadsPopup::rebuild()
{
    for (const RowWidgets &row : std::as_const(m_rows))
        if (row.container) row.container->deleteLater();
    m_rows.clear();

    for (const DownloadEntry &entry : m_manager->entries())
        addRow(entry);
    updateHeader();
}

void DownloadsPopup::addRow(const DownloadEntry &entry)
{
    auto *container = new QFrame(m_listWidget);
    container->setObjectName("downloadRow");
    auto *outer = new QHBoxLayout(container);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(8);

    auto *info = new QVBoxLayout;
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(4);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);

    auto *title = new QLabel(entryDisplayName(entry), container);
    title->setTextFormat(Qt::PlainText);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *badge = new QLabel(kindLabel(entry.request.kind), container);
    badge->setObjectName("selectedItemDetail");
    badge->setTextFormat(Qt::PlainText);

    top->addWidget(title, 1);
    top->addWidget(badge);
    info->addLayout(top);

    auto *progress = new QProgressBar(container);
    progress->setRange(0, 100);
    progress->setTextVisible(false);
    progress->setFixedHeight(6);

    auto *status = new QLabel(container);
    status->setObjectName("selectedItemDetail");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);

    info->addWidget(progress);
    info->addWidget(status);

    auto *action = new QPushButton(container);
    action->setFlat(true);
    action->setMinimumWidth(64);

    outer->addLayout(info, 1);
    outer->addWidget(action, 0, Qt::AlignVCenter);

    const int jobId = entry.jobId;
    connect(action, &QPushButton::clicked, this, [this, jobId]() {
        const DownloadEntry *current = m_manager->entryById(jobId);
        if (!current) return;
        if (current->state == DownloadEntry::Queued || current->state == DownloadEntry::Active) {
            m_manager->cancel(jobId);
        } else if (current->state == DownloadEntry::Failed || current->state == DownloadEntry::Cancelled) {
            m_manager->retry(jobId);
        } else if (current->state == DownloadEntry::Completed && current->importedId > 0) {
            const QString kind = current->request.kind;
            const int id = current->importedId;
            hide();
            emit showRequested(kind, id);
        }
    });

    // Insert newest at top (index 0)
    m_listLayout->insertWidget(0, container);
    m_rows.insert(jobId, {container, title, badge, status, progress, action});
    updateRow(jobId);
}

void DownloadsPopup::updateRow(int jobId)
{
    const DownloadEntry *entry = m_manager->entryById(jobId);
    if (!entry || !m_rows.contains(jobId)) return;
    const RowWidgets &row = m_rows[jobId];

    switch (entry->state) {
    case DownloadEntry::Queued:
        row.progress->hide();
        row.status->setObjectName("selectedItemDetail");
        row.status->setText(tr("Queued"));
        row.status->show();
        row.action->setText(tr("Cancel"));
        row.action->setEnabled(true);
        break;
    case DownloadEntry::Active:
        row.progress->setValue(entry->percent);
        row.progress->show();
        row.status->setObjectName("selectedItemDetail");
        row.status->setText(entry->percent > 0 ? tr("Downloading... %1%").arg(entry->percent) : tr("Downloading..."));
        row.status->show();
        row.action->setText(tr("Cancel"));
        row.action->setEnabled(true);
        break;
    case DownloadEntry::Completed:
        row.progress->hide();
        row.status->setObjectName("selectedItemDetail");
        row.status->setText(tr("Added"));
        row.status->show();
        row.action->setText(tr("Show"));
        row.action->setEnabled(entry->importedId > 0);
        break;
    case DownloadEntry::Failed:
        row.progress->hide();
        row.status->setObjectName("serverConfigError");
        row.status->setText(entry->error.isEmpty() ? tr("Failed") : entry->error);
        row.status->show();
        row.action->setText(tr("Retry"));
        row.action->setEnabled(true);
        break;
    case DownloadEntry::Cancelled:
        row.progress->hide();
        row.status->setObjectName("selectedItemDetail");
        row.status->setText(tr("Cancelled"));
        row.status->show();
        row.action->setText(tr("Retry"));
        row.action->setEnabled(true);
        break;
    }
    row.status->style()->unpolish(row.status);
    row.status->style()->polish(row.status);
}

void DownloadsPopup::updateHeader()
{
    const bool hasEntries = !m_manager->entries().isEmpty();
    const int active = m_manager->activeCount();
    const bool hasFinished = int(m_manager->entries().size()) > active;
    m_empty->setVisible(!hasEntries);
    m_scroll->setVisible(hasEntries);
    m_clearButton->setVisible(hasFinished);
    m_cancelAllButton->setVisible(active > 0);
}
