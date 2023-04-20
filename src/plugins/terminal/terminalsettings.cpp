// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "terminalsettings.h"

#include "terminaltr.h"

#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/theme/theme.h>

using namespace Utils;

namespace Terminal {

static QString defaultFontFamily()
{
    if (HostOsInfo::isMacHost())
        return QLatin1String("Menlo");

    if (Utils::HostOsInfo::isAnyUnixHost())
        return QLatin1String("Monospace");

    return QLatin1String("Consolas");
}

static int defaultFontSize()
{
    if (Utils::HostOsInfo::isMacHost())
        return 12;
    if (Utils::HostOsInfo::isAnyUnixHost())
        return 9;
    return 10;
}

static QString defaultShell()
{
    if (HostOsInfo::isWindowsHost())
        return qtcEnvironmentVariable("COMSPEC");

    QString defaultShell = qtcEnvironmentVariable("SHELL");
    if (FilePath::fromUserInput(defaultShell).isExecutableFile())
        return defaultShell;

    Utils::FilePath shPath = Utils::Environment::systemEnvironment().searchInPath("sh");
    return shPath.nativePath();
}

TerminalSettings &TerminalSettings::instance()
{
    static TerminalSettings settings;
    return settings;
}

void setupColor(TerminalSettings *settings,
                ColorAspect &color,
                const QString &label,
                const QColor &defaultColor)
{
    color.setSettingsKey(label);
    color.setDefaultValue(defaultColor);
    color.setToolTip(Tr::tr("The color used for %1.").arg(label));

    settings->registerAspect(&color);
}

TerminalSettings::TerminalSettings()
{
    setAutoApply(false);
    setSettingsGroup("Terminal");

    enableTerminal.setSettingsKey("EnableTerminal");
    enableTerminal.setLabelText(Tr::tr("Use internal Terminal"));
    enableTerminal.setToolTip(
        Tr::tr("If enabled, use the internal terminal when \"Run In Terminal\" is "
               "enabled and for \"Open Terminal here\"."));
    enableTerminal.setDefaultValue(true);

    font.setSettingsKey("FontFamily");
    font.setLabelText(Tr::tr("Family:"));
    font.setHistoryCompleter("Terminal.Fonts.History");
    font.setToolTip(Tr::tr("The font family used in the terminal."));
    font.setDefaultValue(defaultFontFamily());

    fontSize.setSettingsKey("FontSize");
    fontSize.setLabelText(Tr::tr("Size:"));
    fontSize.setToolTip(Tr::tr("The font size used in the terminal. (in points)"));
    fontSize.setDefaultValue(defaultFontSize());
    fontSize.setRange(1, 100);

    allowBlinkingCursor.setSettingsKey("AllowBlinkingCursor");
    allowBlinkingCursor.setLabelText(Tr::tr("Allow blinking cursor"));
    allowBlinkingCursor.setToolTip(Tr::tr("Allow the cursor to blink."));
    allowBlinkingCursor.setDefaultValue(false);

    shell.setSettingsKey("ShellPath");
    shell.setLabelText(Tr::tr("Shell path:"));
    shell.setExpectedKind(PathChooser::ExistingCommand);
    shell.setDisplayStyle(StringAspect::PathChooserDisplay);
    shell.setHistoryCompleter("Terminal.Shell.History");
    shell.setToolTip(Tr::tr("The shell executable to be started as terminal"));
    shell.setDefaultValue(defaultShell());

    shellArguments.setSettingsKey("ShellArguments");
    shellArguments.setLabelText(Tr::tr("Shell arguments:"));
    shellArguments.setDisplayStyle(StringAspect::LineEditDisplay);
    shellArguments.setHistoryCompleter("Terminal.Shell.History");
    shellArguments.setToolTip(Tr::tr("The arguments to be passed to the shell"));
    if (!HostOsInfo::isWindowsHost())
        shellArguments.setDefaultValue(QString("-l"));

    sendEscapeToTerminal.setSettingsKey("SendEscapeToTerminal");
    sendEscapeToTerminal.setLabelText(Tr::tr("Send escape key to terminal"));
    sendEscapeToTerminal.setToolTip(
        Tr::tr("If enabled, pressing the escape key will send it to the terminal "
               "instead of closing the terminal."));
    sendEscapeToTerminal.setDefaultValue(false);

    audibleBell.setSettingsKey("AudibleBell");
    audibleBell.setLabelText(Tr::tr("Audible bell"));
    audibleBell.setToolTip(Tr::tr("If enabled, the terminal will beep when a bell "
                                  "character is received."));
    audibleBell.setDefaultValue(true);

    registerAspect(&font);
    registerAspect(&fontSize);
    registerAspect(&shell);
    registerAspect(&allowBlinkingCursor);
    registerAspect(&enableTerminal);
    registerAspect(&sendEscapeToTerminal);
    registerAspect(&audibleBell);
    registerAspect(&shellArguments);

    setupColor(this,
               foregroundColor,
               "Foreground",
               Utils::creatorTheme()->color(Theme::TerminalForeground));
    setupColor(this,
               backgroundColor,
               "Background",
               Utils::creatorTheme()->color(Theme::TerminalBackground));
    setupColor(this,
               selectionColor,
               "Selection",
               Utils::creatorTheme()->color(Theme::TerminalSelection));

    setupColor(this,
               findMatchColor,
               "Find matches",
               Utils::creatorTheme()->color(Theme::TerminalFindMatch));

    setupColor(this, colors[0], "0", Utils::creatorTheme()->color(Theme::TerminalAnsi0));
    setupColor(this, colors[8], "8", Utils::creatorTheme()->color(Theme::TerminalAnsi8));

    setupColor(this, colors[1], "1", Utils::creatorTheme()->color(Theme::TerminalAnsi1));
    setupColor(this, colors[9], "9", Utils::creatorTheme()->color(Theme::TerminalAnsi9));

    setupColor(this, colors[2], "2", Utils::creatorTheme()->color(Theme::TerminalAnsi2));
    setupColor(this, colors[10], "10", Utils::creatorTheme()->color(Theme::TerminalAnsi10));

    setupColor(this, colors[3], "3", Utils::creatorTheme()->color(Theme::TerminalAnsi3));
    setupColor(this, colors[11], "11", Utils::creatorTheme()->color(Theme::TerminalAnsi11));

    setupColor(this, colors[4], "4", Utils::creatorTheme()->color(Theme::TerminalAnsi4));
    setupColor(this, colors[12], "12", Utils::creatorTheme()->color(Theme::TerminalAnsi12));

    setupColor(this, colors[5], "5", Utils::creatorTheme()->color(Theme::TerminalAnsi5));
    setupColor(this, colors[13], "13", Utils::creatorTheme()->color(Theme::TerminalAnsi13));

    setupColor(this, colors[6], "6", Utils::creatorTheme()->color(Theme::TerminalAnsi6));
    setupColor(this, colors[14], "14", Utils::creatorTheme()->color(Theme::TerminalAnsi14));

    setupColor(this, colors[7], "7", Utils::creatorTheme()->color(Theme::TerminalAnsi7));
    setupColor(this, colors[15], "15", Utils::creatorTheme()->color(Theme::TerminalAnsi15));
}

} // namespace Terminal
