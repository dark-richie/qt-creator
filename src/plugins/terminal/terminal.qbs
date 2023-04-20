import qbs 1.0

QtcPlugin {
    name: "Terminal"

    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "vterm" }
    Depends { name: "ptyqt" }

    files: [
        "celliterator.cpp",
        "celliterator.h",
        "glyphcache.cpp",
        "glyphcache.h",
        "keys.cpp",
        "keys.h",
        "scrollback.cpp",
        "scrollback.h",
        "shellmodel.cpp",
        "shellmodel.h",
        "shellintegration.cpp",
        "shellintegration.h",
        "terminal.qrc",
        "terminalcommands.cpp",
        "terminalcommands.h",
        "terminalpane.cpp",
        "terminalpane.h",
        "terminalplugin.cpp",
        "terminalplugin.h",
        "terminalprocessimpl.cpp",
        "terminalprocessimpl.h",
        "terminalsearch.cpp",
        "terminalsearch.h",
        "terminalsettings.cpp",
        "terminalsettings.h",
        "terminalsettingspage.cpp",
        "terminalsettingspage.h",
        "terminalsurface.cpp",
        "terminalsurface.h",
        "terminaltr.h",
        "terminalwidget.cpp",
        "terminalwidget.h",
    ]
}

