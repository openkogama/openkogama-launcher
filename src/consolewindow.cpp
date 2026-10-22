#include "consolewindow.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

constexpr qint64 QuickExitSecs = 15;

QColor errorColor()
{
    return Theme::dark() ? QColor("#e06c75") : QColor("#c0392b");
}

QColor warningColor()
{
    return Theme::dark() ? QColor("#e5c07b") : QColor("#9a6700");
}

bool isCrash(const QString &line)
{
    return line.contains("Crash!!!") || line.contains("fatal content error", Qt::CaseInsensitive);
}

QColor colorFor(const QString &line)
{
    if (isCrash(line) || line.contains("exception", Qt::CaseInsensitive) || line.contains("error", Qt::CaseInsensitive))
        return errorColor();
    if (line.contains("warning", Qt::CaseInsensitive))
        return warningColor();
    return {};
}

}

ConsoleWindow::ConsoleWindow(const QString &title, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(title);
    resize(560, 760);

    auto *copy = new QPushButton(tr("Copy"), this);
    auto *close = new QPushButton(tr("Close"), this);
    connect(copy, &QPushButton::clicked, this, [this]() {
        if (auto *view = qobject_cast<QPlainTextEdit *>(m_tabs->currentWidget()))
            QApplication::clipboard()->setText(view->toPlainText());
    });
    connect(close, &QPushButton::clicked, this, &QWidget::close);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(copy);
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addLayout(buttons);

    connect(&m_timer, &QTimer::timeout, this, &ConsoleWindow::poll);
    m_timer.start(250);
}

void ConsoleWindow::follow(const QString &label, const QString &path)
{
    addSource(label, path, {});
}

void ConsoleWindow::followNewest(const QString &label, const QString &directory, bool onlyNewLines)
{
    QFileInfoList logs = QDir(directory).entryInfoList(QDir::Files, QDir::Time);
    if (onlyNewLines && !logs.isEmpty())
        addSource(label, logs.first().filePath(), directory, logs.first().size());
    else
        addSource(label, {}, directory);
}

void ConsoleWindow::addSource(const QString &label, const QString &path, const QString &directory, qint64 offset)
{
    auto *view = new QPlainTextEdit(m_tabs);
    view->setReadOnly(true);
    view->setMaximumBlockCount(20000);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tabs->addTab(view, label);
    m_sources.append({view, path, directory, offset});
}

void ConsoleWindow::gameFinished(int exitCode, QProcess::ExitStatus status)
{
    poll();
    m_timer.stop();
    if (m_sources.isEmpty())
        return;
    append(0, "[launcher] " + tr("game exited with code %1").arg(exitCode));
    const bool quick = m_started.secsTo(QDateTime::currentDateTime()) < QuickExitSecs;
    if (quick && status == QProcess::NormalExit && exitCode == 0)
        append(0, "[launcher] " + tr("the game closed right after it started, an antivirus may have removed or blocked some of its files, deleting and installing this version again usually helps"));
    if (status == QProcess::CrashExit || exitCode != 0 || quick)
        reportCrash(0);
}

void ConsoleWindow::note(const QString &text)
{
    if (!m_sources.isEmpty())
        append(0, "[launcher] " + text);
}

void ConsoleWindow::poll()
{
    for (int index = 0; index < m_sources.size(); index++) {
        Source &source = m_sources[index];
        if (source.path.isEmpty()) {
            QFileInfoList logs = QDir(source.directory).entryInfoList(QDir::Files, QDir::Time);
            if (logs.isEmpty() || logs.first().lastModified() < m_started)
                continue;
            source.path = logs.first().filePath();
        }

        QFile file(source.path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (file.size() < source.offset) {
            source.offset = 0;
            source.partial.clear();
        }
        file.seek(source.offset);
        QByteArray data = source.partial + file.readAll();
        source.offset = file.pos();

        qsizetype end = data.lastIndexOf('\n');
        source.partial = data.mid(end + 1);
        if (end < 0)
            continue;
        for (const QByteArray &line : data.left(end).split('\n'))
            append(index, QString::fromUtf8(line).trimmed());
    }
}

void ConsoleWindow::append(int source, const QString &line)
{
    if (line.isEmpty())
        return;

    QPlainTextEdit *view = m_sources[source].view;
    QTextCharFormat format;
    if (QColor color = colorFor(line); color.isValid())
        format.setForeground(color);

    QScrollBar *scroll = view->verticalScrollBar();
    bool following = scroll->value() == scroll->maximum();
    QTextCursor cursor(view->document());
    cursor.movePosition(QTextCursor::End);
    if (!view->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, format);
    if (following)
        scroll->setValue(scroll->maximum());

    if (isCrash(line))
        reportCrash(source);
}

void ConsoleWindow::reportCrash(int source)
{
    m_tabs->tabBar()->setTabTextColor(source, errorColor());
    if (m_crashed)
        return;
    m_crashed = true;
    m_tabs->setCurrentIndex(source);
    emit crashed();
}
