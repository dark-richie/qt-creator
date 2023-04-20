// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "helpindexfilter.h"

#include "localhelpmanager.h"
#include "helpmanager.h"
#include "helpplugin.h"
#include "helptr.h"

#include <coreplugin/helpmanager.h>
#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/tasktree.h>
#include <utils/utilsicons.h>

#include <QHelpEngine>
#include <QHelpFilterEngine>
#include <QHelpLink>

using namespace Core;
using namespace Help;
using namespace Help::Internal;

HelpIndexFilter::HelpIndexFilter()
{
    setId("HelpIndexFilter");
    setDisplayName(Tr::tr("Help Index"));
    setDescription(Tr::tr("Locates help topics, for example in the Qt documentation."));
    setDefaultIncludedByDefault(false);
    setDefaultShortcutString("?");
    setRefreshRecipe(Utils::Tasking::Sync([this] { invalidateCache(); return true; }));

    m_icon = Utils::Icons::BOOKMARK.icon();
    connect(Core::HelpManager::Signals::instance(), &Core::HelpManager::Signals::setupFinished,
            this, &HelpIndexFilter::invalidateCache);
    connect(Core::HelpManager::Signals::instance(),
            &Core::HelpManager::Signals::documentationChanged,
            this,
            &HelpIndexFilter::invalidateCache);
    connect(HelpManager::instance(), &HelpManager::collectionFileChanged,
            this, &HelpIndexFilter::invalidateCache);
}

void HelpIndexFilter::prepareSearch(const QString &entry)
{
    Q_UNUSED(entry)
    if (!m_needsUpdate)
        return;

    m_needsUpdate = false;
    LocalHelpManager::setupGuiHelpEngine();
    m_allIndicesCache = LocalHelpManager::filterEngine()->indices({});
    m_lastIndicesCache.clear();
    m_lastEntry.clear();
}

QList<LocatorFilterEntry> HelpIndexFilter::matchesFor(QFutureInterface<LocatorFilterEntry> &future,
                                                      const QString &entry)
{
    const QStringList cache = m_lastEntry.isEmpty() || !entry.contains(m_lastEntry)
                                  ? m_allIndicesCache : m_lastIndicesCache;

    const Qt::CaseSensitivity cs = caseSensitivity(entry);
    QStringList bestKeywords;
    QStringList worseKeywords;
    bestKeywords.reserve(cache.size());
    worseKeywords.reserve(cache.size());
    for (const QString &keyword : cache) {
        if (future.isCanceled())
            return {};
        if (keyword.startsWith(entry, cs))
            bestKeywords.append(keyword);
        else if (keyword.contains(entry, cs))
            worseKeywords.append(keyword);
    }
    m_lastIndicesCache = bestKeywords + worseKeywords;
    m_lastEntry = entry;

    QList<LocatorFilterEntry> entries;
    for (const QString &key : std::as_const(m_lastIndicesCache)) {
        const int index = key.indexOf(entry, 0, cs);
        LocatorFilterEntry filterEntry;
        filterEntry.displayName = key;
        filterEntry.acceptor = [key] {
            HelpPlugin::showLinksInCurrentViewer(LocalHelpManager::linksForKeyword(key), key);
            return AcceptResult();
        };
        filterEntry.displayIcon = m_icon;
        filterEntry.highlightInfo = {index, int(entry.length())};
        entries.append(filterEntry);
    }
    return entries;
}

void HelpIndexFilter::invalidateCache()
{
    m_needsUpdate = true;
}
