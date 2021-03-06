/*
 * Copyright (C) 2016 Canonical Ltd.
 *
 * Contact: Alberto Mardegan <alberto.mardegan@canonical.com>
 *
 * This file is part of account-polld
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AP_ACCOUNT_MANAGER_H
#define AP_ACCOUNT_MANAGER_H

#include <QObject>
#include <QVariantMap>

namespace AccountPolld {

struct AccountData {
    QString pluginId;
    uint accountId;
    QString serviceId;
    QVariantMap auth;

    /* This is needed for using the struct as a QHash key; the "auth" map is
     * intentionally omitted from the comparison as we don't want to use that
     * as a key, too. */
    bool operator==(const AccountData &other) const {
        return pluginId == other.pluginId && accountId == other.accountId &&
            serviceId == other.serviceId;
    }
};

uint qHash(const AccountData &data);

class AppManager;

class AccountManagerPrivate;
class AccountManager: public QObject
{
    Q_OBJECT

public:
    explicit AccountManager(AppManager *appManager, QObject *parent = 0);
    ~AccountManager();

    /* Scan for accounts; for each valid account, the accountReady() signal
     * will be emitted. A finished() signal will be emitted last. */
    void listAccounts();

    /* Call when the authentication data for an account is refused by the
     * server because of token expiration */
    void markAuthFailure(const AccountData &data);

Q_SIGNALS:
    void accountReady(const AccountData &data);
    void finished();

private:
    AccountManagerPrivate *d_ptr;
    Q_DECLARE_PRIVATE(AccountManager)
};

} // namespace

Q_DECLARE_METATYPE(AccountPolld::AccountData)

#endif // AP_ACCOUNT_MANAGER_H
