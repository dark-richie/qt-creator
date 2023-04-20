// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "client.h"
#include "languageclient_global.h"

#include <coreplugin/locator/ilocatorfilter.h>

#include <languageserverprotocol/languagefeatures.h>
#include <languageserverprotocol/lsptypes.h>
#include <languageserverprotocol/workspace.h>

#include <QPointer>
#include <QVector>

namespace Core { class IEditor; }

namespace LanguageClient {

// TODO: Could be public methods of Client instead
Core::LocatorMatcherTask LANGUAGECLIENT_EXPORT workspaceLocatorMatcher(Client *client,
                                                                       int maxResultCount = 0);
Core::LocatorMatcherTask LANGUAGECLIENT_EXPORT workspaceClassMatcher(Client *client,
                                                                     int maxResultCount = 0);
Core::LocatorMatcherTask LANGUAGECLIENT_EXPORT workspaceFunctionMatcher(Client *client,
                                                                        int maxResultCount = 0);

class LanguageClientManager;

class LANGUAGECLIENT_EXPORT DocumentLocatorFilter : public Core::ILocatorFilter
{
    Q_OBJECT
public:
    DocumentLocatorFilter();

    void prepareSearch(const QString &entry) override;
    QList<Core::LocatorFilterEntry> matchesFor(QFutureInterface<Core::LocatorFilterEntry> &future,
                                               const QString &entry) override;
signals:
    void symbolsUpToDate(QPrivateSignal);

protected:
    void forceUse() { m_forced = true; }

    Utils::FilePath m_currentFilePath;

    using DocSymbolGenerator = std::function<Core::LocatorFilterEntry(
        const LanguageServerProtocol::DocumentSymbol &, const Core::LocatorFilterEntry &)>;

    Utils::Link linkForDocSymbol(const LanguageServerProtocol::DocumentSymbol &info) const;
    QList<Core::LocatorFilterEntry> matchesForImpl(
        QFutureInterface<Core::LocatorFilterEntry> &future, const QString &entry,
        const DocSymbolGenerator &docSymbolGenerator);

private:
    void updateCurrentClient();
    void updateSymbols(const LanguageServerProtocol::DocumentUri &uri,
                       const LanguageServerProtocol::DocumentSymbolsResult &symbols);
    void resetSymbols();

    QList<Core::LocatorFilterEntry> entriesForSymbolsInfo(
        const QList<LanguageServerProtocol::SymbolInformation> &infoList,
        const QRegularExpression &regexp);
    QList<Core::LocatorFilterEntry> entriesForDocSymbols(
        const QList<LanguageServerProtocol::DocumentSymbol> &infoList,
        const QRegularExpression &regexp, const DocSymbolGenerator &docSymbolGenerator,
        const Core::LocatorFilterEntry &parent = {});

    QMutex m_mutex;
    QMetaObject::Connection m_updateSymbolsConnection;
    QMetaObject::Connection m_resetSymbolsConnection;
    std::optional<LanguageServerProtocol::DocumentSymbolsResult> m_currentSymbols;
    LanguageServerProtocol::DocumentUri::PathMapper m_pathMapper;
    bool m_forced = false;
    QPointer<DocumentSymbolCache> m_symbolCache;
    LanguageServerProtocol::DocumentUri m_currentUri;
};

class LANGUAGECLIENT_EXPORT WorkspaceLocatorFilter : public Core::ILocatorFilter
{
    Q_OBJECT
public:
    WorkspaceLocatorFilter();

    /// request workspace symbols for all clients with enabled locator
    void prepareSearch(const QString &entry) override;
    QList<Core::LocatorFilterEntry> matchesFor(QFutureInterface<Core::LocatorFilterEntry> &future,
                                               const QString &entry) override;
signals:
    void allRequestsFinished(QPrivateSignal);

protected:
    explicit WorkspaceLocatorFilter(const QVector<LanguageServerProtocol::SymbolKind> &filter);

    /// force request workspace symbols for all given clients
    void prepareSearchForClients(const QString &entry, const QList<Client *> &clients);
    void setMaxResultCount(qint64 limit) { m_maxResultCount = limit; }

private:
    void handleResponse(Client *client,
                        const LanguageServerProtocol::WorkspaceSymbolRequest::Response &response);

    QMutex m_mutex;

    struct SymbolInfoWithPathMapper
    {
        LanguageServerProtocol::SymbolInformation symbol;
        LanguageServerProtocol::DocumentUri::PathMapper mapper;
    };

    QMap<Client *, LanguageServerProtocol::MessageId> m_pendingRequests;
    QVector<SymbolInfoWithPathMapper> m_results;
    QVector<LanguageServerProtocol::SymbolKind> m_filterKinds;
    qint64 m_maxResultCount = 0;
};

class LANGUAGECLIENT_EXPORT WorkspaceClassLocatorFilter : public WorkspaceLocatorFilter
{
public:
    WorkspaceClassLocatorFilter();
};

class LANGUAGECLIENT_EXPORT WorkspaceMethodLocatorFilter : public WorkspaceLocatorFilter
{
public:
    WorkspaceMethodLocatorFilter();
};

} // namespace LanguageClient
