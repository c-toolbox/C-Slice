/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef COPYCREDENTIALS_H
#define COPYCREDENTIALS_H

#include <QList>
#include <QString>

#include <string>

// Network accounts kept in the Windows Credential Manager as generic credentials named
// "C-Opy/<name>". Windows never uses generic credentials to authenticate by itself, so a
// stored password is only applied when C-Opy hands it to a robocopy process.
namespace COpyCredentials {

struct Account {
    QString name;
    QString userName;
};

bool supported();
QList<Account> accounts();
bool store(const QString &name, const QString &userName, const QString &password, QString *error);
bool remove(const QString &name, QString *error);
// The caller must wipe the returned password (SecureZeroMemory) as soon as it has been used.
bool read(const QString &name, QString *userName, std::wstring *password, QString *error);

} // namespace COpyCredentials

#endif // COPYCREDENTIALS_H
