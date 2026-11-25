#include "serverspage.h"
#include "archivebrowser.h"
#include "downloadmanager.h"
#include "settings.h"
#include "tunnel.h"
#include "sharebar.h"
#include "serverconfigdialog.h"
#include "paths.h"
#include "gameruntime.h"
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
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
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
#include <QtEndian>
#include <cstring>

class MockServer : public QTcpServer
{
public:
    QByteArray metadata = "{\"title\":\"A friend's server\",\"thumbnail\":\"/api/server/thumbnail\"}";
    QByteArray image;
    QByteArray list = "[]";
    int metadataStatus = 200;
    int imageStatus = 200;
    QStringList requests;
    QHash<QString, QByteArray> routes;

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
                    if (routes.contains(path)) { status = 200; body = routes.value(path); }
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
            QTRY_VERIFY(!server.requests.filter("/thumbnail?address=").isEmpty());
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
        const QString binary = server + "/" + GameRuntime::serverExecutable();
        QFile::remove(binary);
        QVERIFY(QFile::copy(QApplication::applicationFilePath(), binary));
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
        const QString binary = server + "/" + GameRuntime::serverExecutable();
        QFile::remove(binary);
        QVERIFY(QFile::copy(QApplication::applicationFilePath(), binary));
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

    void archiveMapsBrowseFilterAndImport()
    {
        MockServer server;
        server.routes["/maps/v1/latest.json"] = QByteArrayLiteral(
            "{\"format\":1,\"build\":\"test-1\",\"maps\":2,"
            "\"index\":{\"path\":\"index.json\"},"
            "\"images\":{\"path\":\"../../images/\"},"
            "\"details\":{\"path\":\"details/\",\"perFile\":1024}}");
        server.routes["/maps/v1/builds/test-1/index.json"] = QByteArrayLiteral(
            "{\"fields\":[\"id\",\"site\",\"name\",\"author\",\"authorId\",\"plays\",\"likes\",\"published\",\"format\",\"size\",\"versions\",\"image\"],"
            "\"rows\":["
            "[\"101\",\"www\",\"War Arena\",\"Alice\",\"1\",5000,100,\"2024-01-01\",\"kgmap\",12345,1,\"img1\"],"
            "[\"202\",\"br\",\"Parkour City\",\"Bob\",\"2\",9000,250,\"2025-05-01\",\"kgmap\",54321,1,\"img2\"]"
            "]}");
        server.routes["/maps/v1/builds/test-1/details/000.json"] = QByteArrayLiteral(
            "["
            "[\"desc1\",\"\",\"\",[[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"kgmap\",12345,\"\",\"\",[\"https://cdn.openkogama.org/games/aaa.kgmap\"],\"\"]]],"
            "[\"desc2\",\"\",\"\",[[\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"kgmap\",54321,\"\",\"\",[\"https://cdn.openkogama.org/games/bbb.kgmap\"],\"\"]]]"
            "]");
        server.routes["/maps/v1/images/img1.jpg"] = server.image;
        server.routes["/maps/v1/images/img2.jpg"] = server.image;

        ArchiveBrowser browser(ArchiveBrowser::Maps);
        browser.setEndpoints(QUrl(server.url() + "/maps/v1/"));
        browser.ensureLoaded();

        auto *list = browser.findChild<QListWidget *>("archiveMapsList");
        auto *search = browser.findChild<QLineEdit *>("archiveMapSearch");
        auto *region = browser.findChild<QComboBox *>("archiveMapRegionFilter");
        auto *add = browser.findChild<QPushButton *>("archiveAddMapButton");
        QVERIFY(list && search && region && add);

        // Sorted by Most played by default -> Parkour City (9000) first
        QTRY_COMPARE(list->count(), 2);
        QCOMPARE(list->item(0)->text(), QString("Parkour City"));
        QCOMPARE(list->item(1)->text(), QString("War Arena"));

        // Region filter -> www shows only War Arena
        region->setCurrentIndex(region->findData("www"));
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QString("War Arena"));

        // Reset region and search by author
        region->setCurrentIndex(0);
        search->setText("bob");
        QTRY_COMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QString("Parkour City"));

        // Clicking Add fetches detail chunk and emits ImportRequest
        ImportRequest captured;
        QUrl capturedThumb;
        QObject::connect(&browser, &ArchiveBrowser::importRequested, &browser, [&](const ImportRequest &req, const QUrl &thumb) {
            captured = req;
            capturedThumb = thumb;
        });
        add->click();
        QTRY_COMPARE(captured.kind, QString("map"));
        QCOMPARE(captured.name, QString("Parkour City"));
        QCOMPARE(captured.sha256, QString(64, 'b'));
        QCOMPARE(captured.urls, QList<QUrl>({QUrl("https://cdn.openkogama.org/games/bbb.kgmap")}));
        QCOMPARE(capturedThumb, QUrl(server.url() + "/maps/v1/images/img2.jpg"));
    }

    void archiveAvatarsBrowseFilterAndImport()
    {
        MockServer server;
        server.routes["/avatars/v1/latest.json"] = QByteArrayLiteral(
            "{\"format\":1,\"kind\":\"avatars\",\"build\":\"av-1\",\"avatars\":2,"
            "\"cards\":{\"path\":\"cards/\",\"record\":54,\"perFile\":16384},"
            "\"titles\":\"maps.json\"}");

        auto makeCard = [](uchar shaByte, quint32 maps, quint32 cubes, quint8 sources, quint8 image, quint16 pack, quint32 offset, quint32 length, quint16 mapIdx) {
            QByteArray rec(54, '\0');
            for (int i = 0; i < 32; ++i) rec[i] = char(shaByte);
            qToLittleEndian(maps, reinterpret_cast<uchar *>(rec.data() + 32));
            qToLittleEndian(cubes, reinterpret_cast<uchar *>(rec.data() + 36));
            rec[40] = char(sources);
            rec[41] = char(image);
            qToLittleEndian(pack, reinterpret_cast<uchar *>(rec.data() + 42));
            qToLittleEndian(offset, reinterpret_cast<uchar *>(rec.data() + 44));
            qToLittleEndian(length, reinterpret_cast<uchar *>(rec.data() + 48));
            qToLittleEndian(mapIdx, reinterpret_cast<uchar *>(rec.data() + 52));
            return rec;
        };

        QByteArray cards;
        cards.append(makeCard(0x11, 500, 384, 1, 1, 0x01a, 100, 250, 1));
        cards.append(makeCard(0x22, 999, 400, 1, 1, 0x002, 0, 128, 1)); // marked mature in meta.json -> must be excluded
        server.routes["/avatars/v1/builds/av-1/cards/000.bin"] = cards;
        server.routes["/avatars/v1/builds/av-1/maps.json"] = QByteArrayLiteral("[\"\",\"Test Map\"]");
        server.routes["/avatars/meta.json"] = QByteArrayLiteral(
            "{\"build\":\"av-1\",\"fields\":[\"name\",\"type\",\"desc\",\"flags\",\"rating\",\"tags\"],"
            "\"rows\":{"
            "\"1\":[\"Blue Robot\",\"robot\",\"Cool robot\",\"\",\"\",\"scifi\"],"
            "\"2\":[\"Blocked Avatar\",\"human\",\"Should hide\",\"\",\"mature\",\"\"]"
            "}}");
        server.routes["/avatars/v1/builds/av-1/images/" + QString(64, '1') + ".png"] = server.image;

        ArchiveBrowser browser(ArchiveBrowser::Avatars);
        browser.setEndpoints(QUrl(server.url() + "/avatars/v1/"), QUrl(server.url() + "/avatars/meta.json"));
        browser.ensureLoaded();

        auto *list = browser.findChild<QListWidget *>("archiveAvatarsList");
        auto *typeFilter = browser.findChild<QComboBox *>("archiveAvatarTypeFilter");
        auto *add = browser.findChild<QPushButton *>("archiveAddAvatarButton");
        QVERIFY(list && typeFilter && add);

        // Mature avatar #2 is filtered out; only Blue Robot remains
        QTRY_COMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QString("Blue Robot"));
        QVERIFY(typeFilter->findData("robot") > 0);

        ImportRequest captured;
        QObject::connect(&browser, &ArchiveBrowser::importRequested, &browser, [&](const ImportRequest &req, const QUrl &) {
            captured = req;
        });
        add->click();
        QCOMPARE(captured.kind, QString("avatar"));
        QCOMPARE(captured.name, QString("Blue Robot"));
        QCOMPARE(captured.sha256, QString(64, '1'));
        QCOMPARE(captured.offset, qint64(100));
        QCOMPARE(captured.length, qint64(250));
        QCOMPARE(captured.urls, QList<QUrl>({QUrl(server.url() + "/avatars/v1/builds/av-1/packs/01a.bin")}));
    }

    void archiveModelsMultiSelectAndImport()
    {
        MockServer server;
        server.routes["/models/v2/latest.json"] = QByteArrayLiteral(
            "{\"format\":3,\"build\":\"mod-1\",\"models\":3,"
            "\"cards\":{\"path\":\"cards/\",\"record\":57,\"perFile\":16384},"
            "\"titles\":\"maps.json\"}");

        auto makeModelCard = [](uchar shaByte, quint32 maps, quint32 cubes, float scale, quint8 image, quint16 pack, quint32 offset, quint32 length, quint16 mapIdx) {
            QByteArray rec(57, '\0');
            for (int i = 0; i < 32; ++i) rec[i] = char(shaByte);
            qToLittleEndian(maps, reinterpret_cast<uchar *>(rec.data() + 32));
            qToLittleEndian(cubes, reinterpret_cast<uchar *>(rec.data() + 36));
            quint32 bits = 0;
            std::memcpy(&bits, &scale, sizeof bits);
            qToLittleEndian(bits, reinterpret_cast<uchar *>(rec.data() + 40));
            rec[44] = char(image);
            qToLittleEndian(pack, reinterpret_cast<uchar *>(rec.data() + 45));
            qToLittleEndian(offset, reinterpret_cast<uchar *>(rec.data() + 47));
            qToLittleEndian(length, reinterpret_cast<uchar *>(rec.data() + 51));
            qToLittleEndian(mapIdx, reinterpret_cast<uchar *>(rec.data() + 55));
            return rec;
        };

        QByteArray cards;
        cards.append(makeModelCard(0xaa, 600, 286, 0.25f, 1, 0x005, 100, 300, 1));
        cards.append(makeModelCard(0xbb, 400, 120, 0.5f, 1, 0x007, 50, 200, 1));
        cards.append(makeModelCard(0xcc, 900, 500, 1.0f, 1, 0x009, 0, 100, 1)); // mature -> excluded
        server.routes["/models/v2/builds/mod-1/cards/000.bin"] = cards;
        server.routes["/models/v2/builds/mod-1/maps.json"] = QByteArrayLiteral("[\"\",\"Space Map\"]");
        server.routes["/models/meta.json"] = QByteArrayLiteral(
            "{\"build\":\"mod-1\",\"fields\":[\"name\",\"type\",\"desc\",\"flags\",\"rating\",\"tags\"],"
            "\"rows\":{"
            "\"1\":[\"White Hovercraft\",\"vehicle\",\"Sci-fi craft\",\"\",\"\",\"scifi\"],"
            "\"2\":[\"Round Tree\",\"plant\",\"Green tree\",\"\",\"\",\"nature\"],"
            "\"3\":[\"Hidden Model\",\"other\",\"Blocked\",\"\",\"mature\",\"\"]"
            "}}");

        ArchiveBrowser browser(ArchiveBrowser::Models);
        browser.setEndpoints(QUrl(server.url() + "/models/v2/"), QUrl(server.url() + "/models/meta.json"));
        browser.ensureLoaded();

        auto *list = browser.findChild<QListWidget *>("archiveModelsList");
        auto *add = browser.findChild<QPushButton *>("archiveAddModelButton");
        QVERIFY(list && add);

        // Mature #3 excluded; #1 and #2 remain
        QTRY_COMPARE(list->count(), 2);
        QCOMPARE(list->item(0)->text(), QString("White Hovercraft"));
        QCOMPARE(list->item(1)->text(), QString("Round Tree"));

        // Multi-select both items
        list->selectAll();
        QCOMPARE(list->selectedItems().size(), 2);
        QVERIFY(add->text().contains("2"));

        QList<ImportRequest> captured;
        QObject::connect(&browser, &ArchiveBrowser::importRequested, &browser, [&](const ImportRequest &req, const QUrl &) {
            captured.append(req);
        });
        add->click();
        QCOMPARE(captured.size(), 2);
        QCOMPARE(captured[0].kind, QString("model"));
        QCOMPARE(captured[0].name, QString("White Hovercraft"));
        QCOMPARE(captured[0].scale, QString("0.25"));
        QCOMPARE(captured[0].offset, qint64(100));
        QCOMPARE(captured[0].length, qint64(300));
        QCOMPARE(captured[1].name, QString("Round Tree"));
        QCOMPARE(captured[1].scale, QString("0.5"));
    }

    void downloadManagerQueueAndPopup()
    {
        DownloadManager manager;
        DownloadsPopup popup(&manager);

        ImportRequest req;
        req.kind = "model";
        req.name = "Test Model";
        req.urls = {QUrl("http://127.0.0.1:9/unreachable")};

        // Enqueue 5 jobs -> max 3 running concurrently, 2 queued
        QVector<int> ids;
        for (int i = 0; i < 5; ++i) {
            req.name = QString("Model %1").arg(i + 1);
            ids.append(manager.enqueue(req, {}, "3.5.25"));
        }
        QCOMPARE(manager.entries().size(), 5);
        QVERIFY(manager.runningCount() <= DownloadManager::MaxConcurrent);
        QCOMPARE(manager.activeCount(), 5);

        // Cancel all and clear finished
        manager.cancelAll();
        QCOMPARE(manager.activeCount(), 0);
        for (int id : ids)
            QCOMPARE(manager.entryById(id)->state, DownloadEntry::Cancelled);

        manager.clearFinished();
        QCOMPARE(manager.entries().size(), 0);
    }

    void cleanupTestCase()
    {
        QDir(Paths::data() + "/cache").removeRecursively();
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
