// Copyright (C) 2018 Jochen Seemann
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "conanplugin.h"

#include "conanconstants.h"
#include "conaninstallstep.h"
#include "conansettings.h"

#include <coreplugin/icore.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/target.h>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Conan::Internal {

class ConanPluginPrivate
{
public:
    ConanInstallStepFactory installStepFactory;
};

ConanPlugin::~ConanPlugin()
{
    delete d;
}

void ConanPlugin::initialize()
{
    d = new ConanPluginPrivate;
    conanSettings()->readSettings(ICore::settings());

    connect(ProjectManager::instance(), &ProjectManager::projectAdded,
            this, &ConanPlugin::projectAdded);
}

static void connectTarget(Project *project, Target *target)
{
    if (!ConanPlugin::conanFilePath(project).isEmpty()) {
        const QList<BuildConfiguration *> buildConfigurations = target->buildConfigurations();
        for (BuildConfiguration *buildConfiguration : buildConfigurations)
            buildConfiguration->buildSteps()->insertStep(0, Constants::INSTALL_STEP);
    }
    QObject::connect(target, &Target::addedBuildConfiguration,
                     target, [project] (BuildConfiguration *buildConfiguration) {
        if (!ConanPlugin::conanFilePath(project).isEmpty())
            buildConfiguration->buildSteps()->insertStep(0, Constants::INSTALL_STEP);
    });
}

void ConanPlugin::projectAdded(Project *project)
{
    connect(project, &Project::addedTarget, project, [project] (Target *target) {
        connectTarget(project, target);
    });
}

ConanSettings *ConanPlugin::conanSettings()
{
    static ConanSettings theSettings;
    return &theSettings;
}

FilePath ConanPlugin::conanFilePath(Project *project, const FilePath &defaultFilePath)
{
    const FilePath projectDirectory = project->projectDirectory();
    // conanfile.py takes precedence over conanfile.txt when "conan install dir" is invoked
    const FilePath conanPy = projectDirectory / "conanfile.py";
    if (conanPy.exists())
        return conanPy;
    const FilePath conanTxt = projectDirectory / "conanfile.txt";
    if (conanTxt.exists())
        return conanTxt;
    return defaultFilePath;
}

} // Conan::Internal
