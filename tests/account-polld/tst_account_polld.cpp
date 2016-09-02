/*
 * Copyright (C) 2016 Canonical Ltd.
 *
 * Contact: Alberto Mardegan <alberto.mardegan@canonical.com>
 *
 * This file is part of account-polld
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "fake_push_client.h"

#include <Accounts/Account>
#include <Accounts/Manager>
#include <Accounts/Service>
#include <QDBusPendingReply>
#include <QDebug>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <libqtdbusmock/DBusMock.h>
#include <libqtdbustest/QProcessDBusService.h>

using namespace QtDBusMock;

#define ACCOUNT_POLLD_OBJECT_PATH \
    QStringLiteral("/com/ubuntu/AccountPolld")
#define ACCOUNT_POLLD_SERVICE_NAME \
    QStringLiteral("com.ubuntu.AccountPolld")
#define ACCOUNT_POLLD_INTERFACE ACCOUNT_POLLD_SERVICE_NAME

class AccountPolldTest: public QObject
{
    Q_OBJECT

public:
    AccountPolldTest();

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testNoAccounts();
    void testWithoutAuthentication_data();
    void testWithoutAuthentication();

Q_SIGNALS:
    void pollDone();

private:
    void writePluginsFile(const QString &contents);
    void setupEnvironment();
    void clearBaseDir();
    QDBusPendingReply<void> callPoll() {
        QDBusMessage msg =
            QDBusMessage::createMethodCall(ACCOUNT_POLLD_SERVICE_NAME,
                                           ACCOUNT_POLLD_OBJECT_PATH,
                                           ACCOUNT_POLLD_INTERFACE,
                                           "Poll");
        return m_conn.asyncCall(msg);
    }
    bool replyIsValid(const QDBusMessage &reply);

private:
    QTemporaryDir m_baseDir;
    QString m_pluginsFilePath;
    QtDBusTest::DBusTestRunner m_dbus;
    QtDBusMock::DBusMock m_mock;
    QDBusConnection m_conn;
    QtDBusTest::DBusServicePtr m_accountPolld;
    FakePushClient m_pushClient;
};

AccountPolldTest::AccountPolldTest():
    QObject(0),
    m_dbus((setupEnvironment(), DBUS_SESSION_CONFIG_FILE)),
    m_mock(m_dbus),
    m_conn(m_dbus.sessionConnection()),
    m_accountPolld(new QtDBusTest::QProcessDBusService(ACCOUNT_POLLD_SERVICE_NAME,
                                                       QDBusConnection::SessionBus,
                                                       ACCOUNT_POLLD_BINARY,
                                                       QStringList())),
    m_pushClient(&m_mock)
{
    DBusMock::registerMetaTypes();

    m_conn.connect(ACCOUNT_POLLD_SERVICE_NAME,
                   ACCOUNT_POLLD_OBJECT_PATH,
                   ACCOUNT_POLLD_INTERFACE,
                   "Done",
                   this, SIGNAL(pollDone()));
}

void AccountPolldTest::writePluginsFile(const QString &contents)
{
    QFile file(m_pluginsFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not write file" << m_pluginsFilePath;
        return;
    }

    file.write(contents.toUtf8());
}

void AccountPolldTest::setupEnvironment()
{
    QVERIFY(m_baseDir.isValid());
    QByteArray baseDirPath = m_baseDir.path().toUtf8();
    QDir baseDir(m_baseDir.path());

    qunsetenv("XDG_DATA_DIR");
    qunsetenv("XDG_DATA_HOME");
    qputenv("HOME", baseDirPath + "/home");

    m_pluginsFilePath =
        baseDir.filePath("home/.local/share/" PLUGIN_DATA_FILE);

    //qputenv("ACCOUNTS", baseDirPath + "/home/.config/libaccounts-glib");
    qputenv("AG_APPLICATIONS", TEST_DATA_DIR);
    qputenv("AG_SERVICES", TEST_DATA_DIR);
    qputenv("AG_SERVICE_TYPES", TEST_DATA_DIR);
    qputenv("AG_PROVIDERS", TEST_DATA_DIR);

    qputenv("SSO_USE_PEER_BUS", "0");

    qputenv("XDG_RUNTIME_DIR", baseDirPath + "/runtime-dir");

    qputenv("AP_LOGGING_LEVEL", "2");

    /* Make sure we accidentally don't talk to the developer's services running
     * in the session bus */
    qunsetenv("DBUS_SESSION_BUS_ADDRESS");
}

bool AccountPolldTest::replyIsValid(const QDBusMessage &msg)
{
    if (msg.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "Error name:" << msg.errorName();
        qDebug() << "Error text:" << msg.errorMessage();
    }
    return msg.type() == QDBusMessage::ReplyMessage;
}

void AccountPolldTest::initTestCase()
{
    m_dbus.registerService(m_accountPolld);
    m_dbus.startServices();
}

void AccountPolldTest::init()
{
    QDir baseDir(m_baseDir.path());

    baseDir.mkpath("home");
    baseDir.mkpath("home/.local/share/account-polld");
    baseDir.mkpath("runtime-dir");
}

void AccountPolldTest::cleanup()
{
    if (QTest::currentTestFailed()) {
        m_baseDir.setAutoRemove(false);
        qDebug() << "Base dir:" << m_baseDir.path();
    } else {
        /* Delete all accounts */
        Accounts::Manager manager;
        Q_FOREACH(Accounts::AccountId id, manager.accountList()) {
            Accounts::Account *account = manager.account(id);
            QVERIFY(account);
            account->remove();
            account->syncAndBlock();
        }
    }
}

void AccountPolldTest::testNoAccounts()
{
    QSignalSpy doneCalled(this, SIGNAL(pollDone()));
    auto call = callPoll();

    QVERIFY(doneCalled.wait());
    QCOMPARE(doneCalled.count(), 1);

    QVERIFY(call.isFinished());
    QVERIFY(replyIsValid(call.reply()));

    /* Check that there are no notifications */
    QList<MethodCall> calls =
        m_pushClient.mockedService().GetMethodCalls("Post");
    QCOMPARE(calls.count(), 0);
}

void AccountPolldTest::testWithoutAuthentication_data()
{
    QTest::addColumn<QString>("plugins");
    QTest::addColumn<QStringList>("expectedAppIds");
    QTest::addColumn<QStringList>("expectedNotifications");

    QTest::newRow("no plugins") <<
        "{}" <<
        QStringList{} <<
        QStringList{};

    QTest::newRow("one plugin") <<
        "{"
        "  \"mail_helper\": {\n"
        "    \"appId\": \"mailer\",\n"
        "    \"exec\": \"" PLUGIN_EXECUTABLE "\",\n"
        "    \"needsAuthenticationData\": false,\n"
        "    \"profile\": \"unconfined\"\n"
        "  }\n"
        "}" <<
        QStringList{} <<
        QStringList{};
}

void AccountPolldTest::testWithoutAuthentication()
{
    QFETCH(QString, plugins);
    QFETCH(QStringList, expectedAppIds);
    QFETCH(QStringList, expectedNotifications);

    /* prepare accounts */
    Accounts::Manager manager;
    Accounts::Service coolShare = manager.service("com.ubuntu.tests_coolshare");
    Accounts::Service coolMail = manager.service("coolmail");

    Accounts::Account *account = manager.createAccount("cool");
    account->setDisplayName("disabled");
    account->setEnabled(false);
    account->syncAndBlock();

    account = manager.createAccount("cool");
    account->setDisplayName("all enabled");
    account->setEnabled(true);
    account->selectService(coolShare);
    account->setEnabled(true);
    account->selectService(coolMail);
    account->setEnabled(true);
    account->syncAndBlock();

    /* write plugins json file */
    writePluginsFile(plugins);

    /* Start polling */
    QSignalSpy doneCalled(this, SIGNAL(pollDone()));
    auto call = callPoll();

    QVERIFY(doneCalled.wait());
    QCOMPARE(doneCalled.count(), 1);

    QVERIFY(call.isFinished());
    QVERIFY(replyIsValid(call.reply()));

    /* Check that there are no notifications */
    QList<MethodCall> calls =
        m_pushClient.mockedService().GetMethodCalls("Post");
    QCOMPARE(calls.count(), expectedAppIds.count());
}

#if 0
void AccountPolldTest::testAuthSessionProcessUi()
{
    QDBusMessage msg = methodCall(SIGNOND_DAEMON_OBJECTPATH,
                                  SIGNOND_DAEMON_INTERFACE,
                                  "getAuthSessionObjectPath");
    msg << uint(0);
    msg << QString("ssotest");
    QDBusMessage reply = connection().call(msg);
    QVERIFY(replyIsValid(reply));
    QString objectPath = reply.arguments()[0].toString();
    QVERIFY(objectPath.startsWith('/'));

    /* prepare SignOnUi */
    m_signonUi.mockedService().ClearCalls().waitForFinished();
    QVariantMap uiReply {
        { "data",
            QVariantMap {
                { "UserName", "the user" },
                { "Secret", "s3c'r3t" },
                { "QueryErrorCode", 0 },
            }
        },
    };
    m_signonUi.setNextReply(uiReply);

    /* Start the authentication, using a mechanism requiring UI interaction */
    QVariantMap sessionData {
        { "Some key", "its value" },
        { "height", 123 },
    };
    msg = methodCall(objectPath, SIGNOND_AUTH_SESSION_INTERFACE, "process");
    msg << sessionData;
    msg << QString("mech2");
    reply = connection().call(msg);
    QVERIFY(replyIsValid(reply));

    QVariantMap response = QDBusReply<QVariantMap>(reply).value();
    QVariantMap expectedResponse {
        { "UserName", "the user" },
    };
    QCOMPARE(response, expectedResponse);
}
#endif

QTEST_GUILESS_MAIN(AccountPolldTest);

#include "tst_account_polld.moc"
