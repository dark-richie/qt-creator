// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "shellintegration.h"

#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/stringutils.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(integrationLog, "qtc.terminal.shellintegration", QtWarningMsg)

using namespace Utils;

namespace Terminal {

struct FileToCopy
{
    FilePath source;
    QString destName;
};

// clang-format off
struct
{
    struct
    {
        FilePath rcFile{":/terminal/shellintegrations/shellintegration-bash.sh"};
    } bash;
    struct
    {
        QList<FileToCopy> files{
            {":/terminal/shellintegrations/shellintegration-env.zsh", ".zshenv"},
            {":/terminal/shellintegrations/shellintegration-login.zsh", ".zlogin"},
            {":/terminal/shellintegrations/shellintegration-profile.zsh", ".zprofile"},
            {":/terminal/shellintegrations/shellintegration-rc.zsh", ".zshrc"}
        };
    } zsh;
    struct
    {
        FilePath script{":/terminal/shellintegrations/shellintegration.ps1"};
    } pwsh;

} filesToCopy;
// clang-format on

bool ShellIntegration::canIntegrate(const Utils::CommandLine &cmdLine)
{
    if (cmdLine.executable().needsDevice())
        return false; // TODO: Allow integration for remote shells

    if (!cmdLine.arguments().isEmpty())
        return false;

    if (cmdLine.executable().baseName() == "bash")
        return true;

    if (cmdLine.executable().baseName() == "zsh")
        return true;

    if (cmdLine.executable().baseName() == "pwsh"
        || cmdLine.executable().baseName() == "powershell") {
        return true;
    }

    return false;
}

void ShellIntegration::onOsc(int cmd, const VTermStringFragment &fragment)
{
    QString d = QString::fromLocal8Bit(fragment.str, fragment.len);
    const auto [command, data] = Utils::splitAtFirst(d, ';');

    if (cmd == 1337) {
        const auto [key, value] = Utils::splitAtFirst(command, '=');
        if (key == QStringView(u"CurrentDir"))
            emit currentDirChanged(FilePath::fromUserInput(value.toString()).path());

    } else if (cmd == 7) {
        emit currentDirChanged(FilePath::fromUserInput(d).path());
    } else if (cmd == 133) {
        qCDebug(integrationLog) << "OSC 133:" << data;
    } else if (cmd == 633 && command.length() == 1) {
        if (command[0] == 'E') {
            CommandLine cmdLine = CommandLine::fromUserInput(data.toString());
            emit commandChanged(cmdLine);
        } else if (command[0] == 'D') {
            emit commandChanged({});
        } else if (command[0] == 'P') {
            const auto [key, value] = Utils::splitAtFirst(data, '=');
            if (key == QStringView(u"Cwd"))
                emit currentDirChanged(value.toString());
        }
    }
}

void ShellIntegration::prepareProcess(Utils::QtcProcess &process)
{
    Environment env = process.environment().hasChanges() ? process.environment()
                                                         : Environment::systemEnvironment();
    CommandLine cmd = process.commandLine();

    if (!canIntegrate(cmd))
        return;

    env.set("VSCODE_INJECTION", "1");

    if (cmd.executable().baseName() == "bash") {
        const FilePath rcPath = filesToCopy.bash.rcFile;
        const FilePath tmpRc = FilePath::fromUserInput(
            m_tempDir.filePath(filesToCopy.bash.rcFile.fileName()));
        rcPath.copyFile(tmpRc);

        cmd.addArgs({"--init-file", tmpRc.nativePath()});
    } else if (cmd.executable().baseName() == "zsh") {
        for (const FileToCopy &file : filesToCopy.zsh.files) {
            const auto copyResult = file.source.copyFile(
                FilePath::fromUserInput(m_tempDir.filePath(file.destName)));
            QTC_ASSERT_EXPECTED(copyResult, return);
        }

        const Utils::FilePath originalZdotDir = FilePath::fromUserInput(
            env.value_or("ZDOTDIR", QDir::homePath()));

        env.set("ZDOTDIR", m_tempDir.path());
        env.set("USER_ZDOTDIR", originalZdotDir.nativePath());
    } else if (cmd.executable().baseName() == "pwsh"
               || cmd.executable().baseName() == "powershell") {
        const FilePath rcPath = filesToCopy.pwsh.script;
        const FilePath tmpRc = FilePath::fromUserInput(
            m_tempDir.filePath(filesToCopy.pwsh.script.fileName()));
        rcPath.copyFile(tmpRc);

        cmd.addArgs(QString("-noexit -command try { . \"%1\" } catch {}{1}").arg(tmpRc.nativePath()),
                    CommandLine::Raw);
    }

    process.setCommand(cmd);
    process.setEnvironment(env);
}

} // namespace Terminal
