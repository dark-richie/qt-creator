// Copyright (C) 2019 Luxoft Sweden AB
// Copyright (C) 2018 Pelagicore AG
// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "appmanagermakeinstallstep.h"

#include "appmanagerconstants.h"
#include "appmanagertargetinformation.h"

#include <projectexplorer/processparameters.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace AppManager {
namespace Internal {

class AppManagerMakeInstallStep final : public ProjectExplorer::MakeStep
{
    Q_DECLARE_TR_FUNCTIONS(AppManager::Internal::AppManagerMakeInstallStep)

public:
    AppManagerMakeInstallStep(BuildStepList *bsl, Utils::Id id)
        : MakeStep(bsl, id)
    {
        setSelectedBuildTarget("install");
    }

    bool init() final
    {
        if (!MakeStep::init())
            return false;

        const auto targetInformation = TargetInformation(target());
        if (!targetInformation.isValid())
            return false;

        const auto buildDirectoryPath = targetInformation.buildDirectory.absolutePath();
        if (buildDirectoryPath.isEmpty())
            return false;

        const auto buildDirectoryPathQuoted = ProcessArgs::quoteArg(QDir::toNativeSeparators(buildDirectoryPath));
        const auto installRoot = QString("INSTALL_ROOT=%1").arg(buildDirectoryPathQuoted);

        processParameters()->setWorkingDirectory(FilePath::fromString(buildDirectoryPath));

        CommandLine cmd = processParameters()->command();
        cmd.addArg(installRoot);
        processParameters()->setCommandLine(cmd);

        return true;
    }
};

// Factory

AppManagerMakeInstallStepFactory::AppManagerMakeInstallStepFactory()
{
    registerStep<AppManagerMakeInstallStep>(Constants::MAKE_INSTALL_STEP_ID);
    setDisplayName(AppManagerMakeInstallStep::tr("Make install"));
    setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_DEPLOY);
}

} // namespace Internal
} // namespace AppManager
