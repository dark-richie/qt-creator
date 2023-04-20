// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "copilotsettings.h"
#include "copilottr.h"

#include <utils/algorithm.h>
#include <utils/environment.h>

using namespace Utils;

namespace Copilot {

CopilotSettings &CopilotSettings::instance()
{
    static CopilotSettings settings;
    return settings;
}

CopilotSettings::CopilotSettings()
{
    setAutoApply(false);

    const FilePath nodeFromPath = FilePath("node").searchInPath();

    const FilePaths searchDirs
        = {FilePath::fromUserInput("~/.vim/pack/github/start/copilot.vim/copilot/dist/agent.js"),
           FilePath::fromUserInput(
               "~/.config/nvim/pack/github/start/copilot.vim/copilot/dist/agent.js"),
           FilePath::fromUserInput(
               "~/vimfiles/pack/github/start/copilot.vim/copilot/dist/agent.js"),
           FilePath::fromUserInput(
               "~/AppData/Local/nvim/pack/github/start/copilot.vim/copilot/dist/agent.js")};

    const FilePath distFromVim = Utils::findOrDefault(searchDirs, [](const FilePath &fp) {
        return fp.exists();
    });

    nodeJsPath.setExpectedKind(PathChooser::ExistingCommand);
    nodeJsPath.setDefaultFilePath(nodeFromPath);
    nodeJsPath.setSettingsKey("Copilot.NodeJsPath");
    nodeJsPath.setDisplayStyle(StringAspect::PathChooserDisplay);
    nodeJsPath.setLabelText(Tr::tr("Node.js path:"));
    nodeJsPath.setHistoryCompleter("Copilot.NodePath.History");
    nodeJsPath.setDisplayName(Tr::tr("Node.js Path"));
    nodeJsPath.setToolTip(
        Tr::tr("Select path to node.js executable. See https://nodejs.org/de/download/"
               "for installation instructions."));

    distPath.setExpectedKind(PathChooser::File);
    distPath.setDefaultFilePath(distFromVim);
    distPath.setSettingsKey("Copilot.DistPath");
    distPath.setDisplayStyle(StringAspect::PathChooserDisplay);
    distPath.setLabelText(Tr::tr("Path to agent.js:"));
    distPath.setToolTip(Tr::tr(
        "Select path to agent.js in copilot neovim plugin. See "
        "https://github.com/github/copilot.vim#getting-started for installation instructions."));
    distPath.setHistoryCompleter("Copilot.DistPath.History");
    distPath.setDisplayName(Tr::tr("Agent.js path"));

    registerAspect(&nodeJsPath);
    registerAspect(&distPath);
}

} // namespace Copilot
