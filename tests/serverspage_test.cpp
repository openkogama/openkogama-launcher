#include "serverspage.h"
#include "settings.h"
#include "tunnel.h"
#include "sharebar.h"
#include "serverconfigdialog.h"
#include "paths.h"
#include "gamelauncher.h"
#include "centereddelegate.h"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QTabBar>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalSpy>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTranslator>
#include <QUrl>
#include <QUrlQuery>

class MockServer : public QTcpServer
{
public:
    QByteArray metadata = "{\"title\":\"A friend's server\",\"thumbnail\":\"/api/server/thumbnail\"}";
    QByteArray image;
    QByteArray list = "[]";
    int metadataStatus = 200;
    int imageStatus = 200;
    QStringList requests;

    MockServer()
    {
        listen(QHostAddress::LocalHost);
        QImage tile(8, 8, QImage::Format_RGB32);
        tile.fill(Qt::red);
        QBuffer buffer(&image);
        buffer.open(QIODevice::WriteOnly);
        tile.save(&buffer, "PNG");
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, data = QByteArray()]() mutable {
                    data += socket->readAll();
                    if (!data.contains("\r\n\r\n")) return;
                    const QString path = QString::fromLatin1(data.split(' ').value(1));
                    requests.append(path);
                    int status = path == "/api/server" ? metadataStatus : path == "/api/server/thumbnail" ? imageStatus : 404;
                    QByteArray body = path == "/api/server" ? metadata : path == "/api/server/thumbnail" ? image : QByteArray();
                    if (path == "/ping") { status = 200; body = "{}"; }
                    if (path == "/servers") { status = 200; body = list; }
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    data.clear();
                });
            }
        });
    }

    QString url() const { return "http://127.0.0.1:" + QString::number(serverPort()); }
};

static QString tile(const QListWidgetItem *item)
{
    return item->text() + "\n" + item->data(CenteredDelegate::SubtitleRole).toString();
}

class ServersPageTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setOrganizationName("OpenKogamaTests");
        QApplication::setApplicationName("Launcher-" + QString::number(QApplication::applicationPid()));
    }

    void init()
    {
        Settings::setPublicView(false);
        QSettings(Paths::data() + "/settings.ini", QSettings::IniFormat).setValue("launcher/serverList", "http://127.0.0.1:9");
    }

    void cleanup() { Settings::setServers({}); Settings::setCurrentServer({}); }

    void metadataJoinAndOfflineCache()
    {
        MockServer server;
        QVERIFY(server.isListening());
        Settings::setServers({{"My friend", server.url(), {}, {}}});
        QByteArray cached;
        {
            ServersPage page;
            auto *list = page.findChild<QListWidget *>("serversList");
            QVERIFY(list);
            QTRY_VERIFY(tile(list->item(0)).contains("Online"));
            QTRY_VERIFY(!Settings::servers().first().thumbnail.isEmpty());
            QCOMPARE(tile(list->item(0)), QString("A friend's server\nOnline"));
            QCOMPARE(server.requests, QStringList({"/api/server", "/api/server/thumbnail"}));
            cached = Settings::servers().first().thumbnail;
            QCOMPARE(QImage::fromData(cached).size(), QSize(400, 160));
            QSignalSpy joined(&page, &ServersPage::joinRequested);
            auto *join = page.findChild<QPushButton *>("joinServerButton");
            QVERIFY(!join->isEnabled());
            page.setCanJoin(true);
            join->click();
            QCOMPARE(joined.size(), 1);
            QCOMPARE(joined.first().first().toString(), server.url());
            server.close();
            emit page.findChild<QShortcut *>("refreshServersShortcut")->activated();
            QTRY_COMPARE(tile(list->item(0)), QString("A friend's server\nOffline"));
            QCOMPARE(Settings::servers().first().thumbnail, cached);
        }
        ServersPage reopened;
        auto *list = reopened.findChild<QListWidget *>("serversList");
        QTRY_COMPARE(tile(list->item(0)), QString("A friend's server\nOffline"));
        QCOMPARE(Settings::servers().first().thumbnail, cached);
    }

    void rejectsOversizedImageAndTruncatesTitle()
    {
        MockServer server;
        server.metadata = QJsonDocument(QJsonObject{{"title", QString(80, 'A')},
            {"thumbnail", "/api/server/thumbnail"}, {"worlds", "ignored"}}).toJson();
        server.image = QByteArray(2 * 1024 * 1024 + 1, 'X');
        Settings::setServers({{"Saved", server.url(), {}, {}}});
        ServersPage page;
        auto *list = page.findChild<QListWidget *>("serversList");
        QTRY_COMPARE(Settings::servers().first().title.size(), 40);
        QTRY_VERIFY(server.requests.contains("/api/server/thumbnail"));
        QTest::qWait(100);
        QVERIFY(Settings::servers().first().thumbnail.isEmpty());
        QCOMPARE(tile(list->item(0)), QString(40, 'A') + "\nOnline");
        QCOMPARE(server.requests.size(), 2);
    }

    void rejectsInvalidImagesAndExternalThumbnailURLs()
    {
        MockServer server;
        server.image = "not an image";
        Settings::setServers({{"Saved", server.url(), {}, {}}});
        ServersPage page;
        QTRY_VERIFY(server.requests.contains("/api/server/thumbnail"));
        QTest::qWait(100);
        QVERIFY(Settings::servers().first().thumbnail.isEmpty());
        server.requests.clear();
        server.metadata = "{\"title\":\"Safe\",\"thumbnail\":\"https://example.com/image.png\"}";
        emit page.findChild<QShortcut *>("refreshServersShortcut")->activated();
        QTRY_COMPARE(Settings::servers().first().title, QString("Safe"));
        QCOMPARE(server.requests, QStringList({"/api/server"}));
    }

    void oldServerUsesOnlyPingFallback()
    {
        MockServer server;
        server.metadataStatus = 404;
        Settings::setServers({{"Old server", server.url(), {}, {}}});
        ServersPage page;
        auto *list = page.findChild<QListWidget *>("serversList");
        QTRY_COMPARE(tile(list->item(0)), QString("Old server\nOnline"));
        QCOMPARE(server.requests, QStringList({"/api/server", "/ping"}));
    }

    void preservesUnicodeTitles()
    {
        MockServer server;
        server.metadata = QJsonDocument(QJsonObject{{"title", QString::fromUtf8("😀").repeated(45)}}).toJson();
        Settings::setServers({{"Saved", server.url(), {}, {}}});
        ServersPage page;
        QTRY_COMPARE(Settings::servers().first().title.toUcs4().size(), 40);
    }

    void publicListShowsAndSavesServers()
    {
        MockServer server;
        server.list = QJsonDocument(QJsonArray{
            QJsonObject{{"address", server.url()}, {"title", "Public one"}, {"clients", QJsonArray{"3.5.20"}}, {"players", 3}, {"thumbnail", true}},
            QJsonObject{{"address", "file:///etc/passwd"}, {"title", "Bad"}},
            QJsonObject{{"address", "p2p:abcdef"}, {"title", "Hosted"}, {"players", 1}},
        }).toJson();
        QSettings(Paths::data() + "/settings.ini", QSettings::IniFormat).setValue("launcher/serverList", server.url());
        Settings::setPublicView(true);
        {
            ServersPage page;
            auto *list = page.findChild<QListWidget *>("serversList");
            QTRY_COMPARE(list->count(), 2);
            QCOMPARE(tile(list->item(0)), QString::fromUtf8("Public one\n3 players · 3.5.20"));
            QCOMPARE(tile(list->item(1)), QString("Hosted\n1 player"));
            QVERIFY(page.isPublic(server.url()));
            QVERIFY(page.isPublic("p2p:ABCDEF"));
            QCOMPARE(page.clientsOf(server.url()), QStringList({"3.5.20"}));
            QTRY_VERIFY(server.requests.contains("/api/server/thumbnail"));
            list->setCurrentRow(0);
            QAction *save = nullptr;
            for (QAction *action : page.findChild<QToolButton *>("moreServerButton")->menu()->actions())
                if (action->text() == "Save to my servers") save = action;
            QVERIFY(save && save->isVisible() && save->isEnabled());
            save->trigger();
            QCOMPARE(Settings::servers().size(), 1);
            QCOMPARE(Settings::servers().first().title, QString("Public one"));
            QCOMPARE(Settings::servers().first().clients, QStringList({"3.5.20"}));
            QVERIFY(!save->isEnabled());
            QObject game;
            page.reportPlaying(&game, server.url());
            QTRY_VERIFY(server.requests.contains("/playing"));
            page.findChild<QTabBar *>("serversViewTabs")->setCurrentIndex(1);
            QCOMPARE(list->count(), 1);
            QCOMPARE(list->item(0)->text(), QString("Public one"));
        }
        QVERIFY(!Settings::publicView());
        QSettings(Paths::data() + "/settings.ini", QSettings::IniFormat).remove("launcher/serverList");
    }

    void joinStartsGameWithServerSession()
    {
        QTemporaryDir instance;
        QVERIFY(instance.isValid());
        QFile meta(instance.filePath("instance.json"));
        QVERIFY(meta.open(QIODevice::WriteOnly));
        meta.write("{\"name\":\"Test\",\"version\":\"2015\",\"unityVersion\":\"5.2\"}");
        meta.close();
        QWidget owner;
        QPointer<QProcess> client = GameLauncher::start(instance.path(), QApplication::applicationFilePath(),
            "http://127.0.0.1:8080", "play", 0, &owner, &owner);
        QVERIFY(client);
        const QString capture = instance.filePath("launch-capture.json");
        QTRY_VERIFY(QFile::exists(capture));
        QFile result(capture);
        QVERIFY(result.open(QIODevice::ReadOnly));
        const QJsonObject launched = QJsonDocument::fromJson(result.readAll()).object();
        const QUrl session(launched.value("session").toString());
        const QUrlQuery query(session);
        QCOMPARE(session.toString(QUrl::RemoveQuery), QString("http://127.0.0.1:8080/session"));
        QCOMPARE(query.queryItemValue("mode"), QString("play"));
        QCOMPARE(query.queryItemValue("client"), QString("2015"));
        QCOMPARE(query.queryItemValue("unity"), QString("5.2"));
        QVERIFY(!query.queryItemValue("key").isEmpty());
        QCOMPARE(launched.value("cwd").toString(), instance.path());
        QTRY_VERIFY(!client || client->state() == QProcess::NotRunning);
    }

    void codeShowsHostedWorld()
    {
        QCOMPARE(Tunnel::normalize("abc-def"), QString("ABCDEF"));
        QCOMPARE(Tunnel::normalize(" ABC DEF "), QString("ABCDEF"));
        QVERIFY(Tunnel::normalize("ABCDE1").isEmpty());
        QVERIFY(Tunnel::normalize("ABCDEFG").isEmpty());
        QCOMPARE(Tunnel::display("ABCDEF"), QString("ABC-DEF"));
        const QString server = QApplication::applicationDirPath() + "/server";
        QVERIFY(QDir().mkpath(server));
        QFile::remove(server + "/openkogama-server.exe");
        QVERIFY(QFile::copy(QApplication::applicationFilePath(), server + "/openkogama-server.exe"));
        Settings::setServers({{"Code ABC-DEF", "p2p:abc-def", {}, {}}});
        QCOMPARE(Settings::servers().first().address, QString("p2p:ABCDEF"));
        {
            ServersPage page;
            auto *list = page.findChild<QListWidget *>("serversList");
            QTRY_COMPARE(tile(list->item(0)), QString::fromUtf8("Fake world (Ann)\nOnline · 3.5.20"));
            QCOMPARE(Settings::servers().first().clients, QStringList({"3.5.20"}));
            QCOMPARE(page.findChild<QLabel *>("selectedServerAddress")->text(), QString("ABC-DEF"));
        }
        QDir(server).removeRecursively();
    }

    void hostingShowsCodeAndFriends()
    {
        const QString server = QApplication::applicationDirPath() + "/server";
        QVERIFY(QDir().mkpath(server));
        QFile::remove(server + "/openkogama-server.exe");
        QVERIFY(QFile::copy(QApplication::applicationFilePath(), server + "/openkogama-server.exe"));
        {
            ShareBar bar;
            bar.share(4, "Fake world", "3.5.20");
            QVERIFY(!bar.isHidden());
            QTRY_COMPARE(bar.findChild<QLabel *>("shareCode")->text(), QString("Code ABC-DEF"));
            QTRY_COMPARE(bar.findChild<QLabel *>("shareInfo")->text(), QString("1 player joined"));
            QCOMPARE(bar.players(), 1);
            bar.findChild<QPushButton *>("shareStopButton")->click();
            QVERIFY(bar.isHidden());
            QCOMPARE(bar.players(), 0);
        }
        QDir(server).removeRecursively();
    }

    void serverSettingsCheckTheJson()
    {
        ServerConfigDialog dialog;
        auto *edit = dialog.findChild<QPlainTextEdit *>("serverConfigEdit");
        auto *error = dialog.findChild<QLabel *>("serverConfigError");
        auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
        edit->setPlainText("{\n  \"title\": ");
        save->click();
        QVERIFY(!error->isHidden());
        QVERIFY(error->text().startsWith("Line 2"));
        edit->setPlainText("{ \"httpPort\": 9000 }");
        save->click();
        QVERIFY(error->text().contains("httpPort"));
        edit->setPlainText("{ \"title\": \"Test\", \"clients\": [\"3.5.20\"] }");
        save->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ServerConfigDialog::current().value("title").toString(), QString("Test"));
        ServerConfigDialog reopened;
        const QJsonObject shown = QJsonDocument::fromJson(reopened.findChild<QPlainTextEdit *>("serverConfigEdit")->toPlainText().toUtf8()).object();
        QCOMPARE(shown.value("title").toString(), QString("Test"));
        QVERIFY(shown.contains("public") && !shown.value("public").toBool());
        QVERIFY(QFile::remove(QDir(Paths::serverData()).filePath("server.json")));
    }

    void cleanupTestCase()
    {
        const QString settings = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/settings.ini";
        QFile::remove(settings);
        QDir().rmdir(QFileInfo(settings).absolutePath());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString package = app.arguments().value(1);
    if (package == "tunnel") {
        QTextStream out(stdout);
        if (app.arguments().value(2) == "host") {
            out << "code ABCDEF" << Qt::endl << "players 1" << Qt::endl;
            QThread::sleep(5);
            return 0;
        }
        out << "found {\"title\":\"Fake world (Ann)\",\"version\":\"3.5.20\",\"world\":4}" << Qt::endl;
        return 0;
    }
    if (package.startsWith("kogamaPackage:")) {
        QFile capture(QDir::current().filePath("launch-capture.json"));
        if (!capture.open(QIODevice::WriteOnly)) return 1;
        capture.write(QJsonDocument(QJsonObject{{"session", QString::fromUtf8(QByteArray::fromBase64(package.mid(14).toLatin1()))},
            {"cwd", QDir::currentPath()}}).toJson());
        return 0;
    }
    QTranslator english;
    if (english.load("launcher_en", LAUNCHER_TRANSLATIONS)) app.installTranslator(&english);
    ServersPageTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "serverspage_test.moc"
