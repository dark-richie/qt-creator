// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectmanager.h"

#include "session_p.h"
#include "session.h"

#include "buildconfiguration.h"
#include "editorconfiguration.h"
#include "project.h"
#include "projectexplorer.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "projectmanager.h"
#include "projectnodes.h"
#include "target.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/foldernavigationwidget.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/imode.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <texteditor/texteditor.h>

#include <utils/algorithm.h>
#include <utils/filepath.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QMessageBox>
#include <QPushButton>

#ifdef WITH_TESTS
#include <QTemporaryFile>
#include <QTest>
#include <vector>
#endif

using namespace Core;
using namespace Utils;
using namespace ProjectExplorer::Internal;

namespace ProjectExplorer {

const char DEFAULT_SESSION[] = "default";

class ProjectManagerPrivate
{
public:
    void restoreDependencies(const PersistentSettingsReader &reader);
    void restoreStartupProject(const PersistentSettingsReader &reader);
    void restoreProjects(const FilePaths &fileList);
    void askUserAboutFailedProjects();

    bool recursiveDependencyCheck(const FilePath &newDep, const FilePath &checkDep) const;
    FilePaths dependencies(const FilePath &proName) const;
    FilePaths dependenciesOrder() const;
    void dependencies(const FilePath &proName, FilePaths &result) const;

    static QString windowTitleAddition(const FilePath &filePath);
    static QString sessionTitle(const FilePath &filePath);

    bool hasProjects() const { return !m_projects.isEmpty(); }

    bool m_casadeSetActive = false;

    Project *m_startupProject = nullptr;
    QList<Project *> m_projects;
    FilePaths m_failedProjects;
    QMap<FilePath, FilePaths> m_depMap;

private:
    static QString locationInProject(const FilePath &filePath);
};

static ProjectManager *m_instance = nullptr;
static ProjectManagerPrivate *d = nullptr;

static QString projectFolderId(Project *pro)
{
    return pro->projectFilePath().toString();
}

const int PROJECT_SORT_VALUE = 100;

ProjectManager::ProjectManager()
{
    m_instance = this;
    d = new ProjectManagerPrivate;

    connect(EditorManager::instance(), &EditorManager::editorCreated,
            this, &ProjectManager::configureEditor);
    connect(this, &ProjectManager::projectAdded,
            EditorManager::instance(), &EditorManager::updateWindowTitles);
    connect(this, &ProjectManager::projectRemoved,
            EditorManager::instance(), &EditorManager::updateWindowTitles);
    connect(this, &ProjectManager::projectDisplayNameChanged,
            EditorManager::instance(), &EditorManager::updateWindowTitles);

    EditorManager::setWindowTitleAdditionHandler(&ProjectManagerPrivate::windowTitleAddition);
    EditorManager::setSessionTitleHandler(&ProjectManagerPrivate::sessionTitle);
}

ProjectManager::~ProjectManager()
{
    EditorManager::setWindowTitleAdditionHandler({});
    EditorManager::setSessionTitleHandler({});
    delete d;
    d = nullptr;
}

ProjectManager *ProjectManager::instance()
{
   return m_instance;
}

bool ProjectManagerPrivate::recursiveDependencyCheck(const FilePath &newDep,
                                                     const FilePath &checkDep) const
{
    if (newDep == checkDep)
        return false;

    const FilePaths depList = m_depMap.value(checkDep);
    for (const FilePath &dependency : depList) {
        if (!recursiveDependencyCheck(newDep, dependency))
            return false;
    }

    return true;
}

/*
 * The dependency management exposes an interface based on projects, but
 * is internally purely string based. This is suboptimal. Probably it would be
 * nicer to map the filenames to projects on load and only map it back to
 * filenames when saving.
 */

QList<Project *> ProjectManager::dependencies(const Project *project)
{
    const FilePath proName = project->projectFilePath();
    const FilePaths proDeps = d->m_depMap.value(proName);

    QList<Project *> projects;
    for (const FilePath &dep : proDeps) {
        Project *pro = Utils::findOrDefault(d->m_projects, [&dep](Project *p) {
            return p->projectFilePath() == dep;
        });
        if (pro)
            projects += pro;
    }

    return projects;
}

bool ProjectManager::hasDependency(const Project *project, const Project *depProject)
{
    const FilePath proName = project->projectFilePath();
    const FilePath depName = depProject->projectFilePath();

    const FilePaths proDeps = d->m_depMap.value(proName);
    return proDeps.contains(depName);
}

bool ProjectManager::canAddDependency(const Project *project, const Project *depProject)
{
    const FilePath newDep = project->projectFilePath();
    const FilePath checkDep = depProject->projectFilePath();

    return d->recursiveDependencyCheck(newDep, checkDep);
}

bool ProjectManager::addDependency(Project *project, Project *depProject)
{
    const FilePath proName = project->projectFilePath();
    const FilePath depName = depProject->projectFilePath();

    // check if this dependency is valid
    if (!d->recursiveDependencyCheck(proName, depName))
        return false;

    FilePaths proDeps = d->m_depMap.value(proName);
    if (!proDeps.contains(depName)) {
        proDeps.append(depName);
        d->m_depMap[proName] = proDeps;
    }
    emit m_instance->dependencyChanged(project, depProject);

    return true;
}

void ProjectManager::removeDependency(Project *project, Project *depProject)
{
    const FilePath proName = project->projectFilePath();
    const FilePath depName = depProject->projectFilePath();

    FilePaths proDeps = d->m_depMap.value(proName);
    proDeps.removeAll(depName);
    if (proDeps.isEmpty())
        d->m_depMap.remove(proName);
    else
        d->m_depMap[proName] = proDeps;
    emit m_instance->dependencyChanged(project, depProject);
}

bool ProjectManager::isProjectConfigurationCascading()
{
    return d->m_casadeSetActive;
}

void ProjectManager::setProjectConfigurationCascading(bool b)
{
    d->m_casadeSetActive = b;
    SessionManager::markSessionFileDirty();
}

void ProjectManager::setStartupProject(Project *startupProject)
{
    QTC_ASSERT((!startupProject && d->m_projects.isEmpty())
               || (startupProject && d->m_projects.contains(startupProject)), return);

    if (d->m_startupProject == startupProject)
        return;

    d->m_startupProject = startupProject;
    if (d->m_startupProject && d->m_startupProject->needsConfiguration()) {
        ModeManager::activateMode(Constants::MODE_SESSION);
        ModeManager::setFocusToCurrentMode();
    }
    FolderNavigationWidgetFactory::setFallbackSyncFilePath(
        startupProject ? startupProject->projectFilePath().parentDir() : FilePath());
    emit m_instance->startupProjectChanged(startupProject);
}

Project *ProjectManager::startupProject()
{
    return d->m_startupProject;
}

Target *ProjectManager::startupTarget()
{
    return d->m_startupProject ? d->m_startupProject->activeTarget() : nullptr;
}

BuildSystem *ProjectManager::startupBuildSystem()
{
    Target *t = startupTarget();
    return t ? t->buildSystem() : nullptr;
}

/*!
 * Returns the RunConfiguration of the currently active target
 * of the startup project, if such exists, or \c nullptr otherwise.
 */


RunConfiguration *ProjectManager::startupRunConfiguration()
{
    Target *t = startupTarget();
    return t ? t->activeRunConfiguration() : nullptr;
}

void ProjectManager::addProject(Project *pro)
{
    QTC_ASSERT(pro, return);
    QTC_CHECK(!pro->displayName().isEmpty());
    QTC_CHECK(pro->id().isValid());

    sb_d->m_virginSession = false;
    QTC_ASSERT(!d->m_projects.contains(pro), return);

    d->m_projects.append(pro);

    connect(pro, &Project::displayNameChanged,
            m_instance, [pro]() { emit m_instance->projectDisplayNameChanged(pro); });

    emit m_instance->projectAdded(pro);
    const auto updateFolderNavigation = [pro] {
        // destructing projects might trigger changes, so check if the project is actually there
        if (QTC_GUARD(d->m_projects.contains(pro))) {
            const QIcon icon = pro->rootProjectNode() ? pro->rootProjectNode()->icon() : QIcon();
            FolderNavigationWidgetFactory::insertRootDirectory({projectFolderId(pro),
                                                                PROJECT_SORT_VALUE,
                                                                pro->displayName(),
                                                                pro->projectFilePath().parentDir(),
                                                                icon});
        }
    };
    updateFolderNavigation();
    configureEditors(pro);
    connect(pro, &Project::fileListChanged, m_instance, [pro, updateFolderNavigation]() {
        configureEditors(pro);
        updateFolderNavigation(); // update icon
    });
    connect(pro, &Project::displayNameChanged, m_instance, updateFolderNavigation);

    if (!startupProject())
        setStartupProject(pro);
}

void ProjectManager::removeProject(Project *project)
{
    sb_d->m_virginSession = false;
    QTC_ASSERT(project, return);
    removeProjects({project});
}

bool ProjectManager::save()
{
    emit SessionManager::instance()->aboutToSaveSession();

    const FilePath filePath = SessionManager::sessionNameToFileName(sb_d->m_sessionName);
    QVariantMap data;

    // See the explanation at loadSession() for how we handle the implicit default session.
    if (SessionManager::isDefaultVirgin()) {
        if (filePath.exists()) {
            PersistentSettingsReader reader;
            if (!reader.load(filePath)) {
                QMessageBox::warning(ICore::dialogParent(), Tr::tr("Error while saving session"),
                                     Tr::tr("Could not save session %1").arg(filePath.toUserOutput()));
                return false;
            }
            data = reader.restoreValues();
        }
    } else {
        // save the startup project
        if (d->m_startupProject)
            data.insert("StartupProject", d->m_startupProject->projectFilePath().toSettings());

        const QColor c = StyleHelper::requestedBaseColor();
        if (c.isValid()) {
            QString tmp = QString::fromLatin1("#%1%2%3")
                    .arg(c.red(), 2, 16, QLatin1Char('0'))
                    .arg(c.green(), 2, 16, QLatin1Char('0'))
                    .arg(c.blue(), 2, 16, QLatin1Char('0'));
            data.insert(QLatin1String("Color"), tmp);
        }

        FilePaths projectFiles = Utils::transform(projects(), &Project::projectFilePath);
        // Restore information on projects that failed to load:
        // don't read projects to the list, which the user loaded
        for (const FilePath &failed : std::as_const(d->m_failedProjects)) {
            if (!projectFiles.contains(failed))
                projectFiles << failed;
        }

        data.insert("ProjectList", Utils::transform<QStringList>(projectFiles,
                                                                 &FilePath::toString));
        data.insert("CascadeSetActive", d->m_casadeSetActive);

        QVariantMap depMap;
        auto i = d->m_depMap.constBegin();
        while (i != d->m_depMap.constEnd()) {
            QString key = i.key().toString();
            QStringList values;
            const FilePaths valueList = i.value();
            for (const FilePath &value : valueList)
                values << value.toString();
            depMap.insert(key, values);
            ++i;
        }
        data.insert(QLatin1String("ProjectDependencies"), QVariant(depMap));
        data.insert(QLatin1String("EditorSettings"), EditorManager::saveState().toBase64());
    }

    const auto end = sb_d->m_values.constEnd();
    QStringList keys;
    for (auto it = sb_d->m_values.constBegin(); it != end; ++it) {
        data.insert(QLatin1String("value-") + it.key(), it.value());
        keys << it.key();
    }
    data.insert(QLatin1String("valueKeys"), keys);

    if (!sb_d->m_writer || sb_d->m_writer->fileName() != filePath) {
        delete sb_d->m_writer;
        sb_d->m_writer = new PersistentSettingsWriter(filePath, "QtCreatorSession");
    }
    const bool result = sb_d->m_writer->save(data, ICore::dialogParent());
    if (result) {
        if (!SessionManager::isDefaultVirgin())
            sb_d->m_sessionDateTimes.insert(SessionManager::activeSession(), QDateTime::currentDateTime());
    } else {
        QMessageBox::warning(ICore::dialogParent(), Tr::tr("Error while saving session"),
            Tr::tr("Could not save session to file %1").arg(sb_d->m_writer->fileName().toUserOutput()));
    }

    return result;
}

/*!
  Closes all projects
  */
void ProjectManager::closeAllProjects()
{
    removeProjects(projects());
}

const QList<Project *> ProjectManager::projects()
{
    return d->m_projects;
}

bool ProjectManager::hasProjects()
{
    return d->hasProjects();
}

bool ProjectManager::hasProject(Project *p)
{
    return d->m_projects.contains(p);
}

FilePaths ProjectManagerPrivate::dependencies(const FilePath &proName) const
{
    FilePaths result;
    dependencies(proName, result);
    return result;
}

void ProjectManagerPrivate::dependencies(const FilePath &proName, FilePaths &result) const
{
    const FilePaths depends = m_depMap.value(proName);

    for (const FilePath &dep : depends)
        dependencies(dep, result);

    if (!result.contains(proName))
        result.append(proName);
}

QString ProjectManagerPrivate::sessionTitle(const FilePath &filePath)
{
    if (SessionManager::isDefaultSession(sb_d->m_sessionName)) {
        if (filePath.isEmpty()) {
            // use single project's name if there is only one loaded.
            const QList<Project *> projects = ProjectManager::projects();
            if (projects.size() == 1)
                return projects.first()->displayName();
        }
    } else {
        QString sessionName = sb_d->m_sessionName;
        if (sessionName.isEmpty())
            sessionName = Tr::tr("Untitled");
        return sessionName;
    }
    return QString();
}

QString ProjectManagerPrivate::locationInProject(const FilePath &filePath)
{
    const Project *project = ProjectManager::projectForFile(filePath);
    if (!project)
        return QString();

    const FilePath parentDir = filePath.parentDir();
    if (parentDir == project->projectDirectory())
        return "@ " + project->displayName();

    if (filePath.isChildOf(project->projectDirectory())) {
        const FilePath dirInProject = parentDir.relativeChildPath(project->projectDirectory());
        return "(" + dirInProject.toUserOutput() + " @ " + project->displayName() + ")";
    }

    // For a file that is "outside" the project it belongs to, we display its
    // dir's full path because it is easier to read than a series of  "../../.".
    // Example: /home/hugo/GenericProject/App.files lists /home/hugo/lib/Bar.cpp
   return "(" + parentDir.toUserOutput() + " @ " + project->displayName() + ")";
}

QString ProjectManagerPrivate::windowTitleAddition(const FilePath &filePath)
{
    return filePath.isEmpty() ? QString() : locationInProject(filePath);
}

FilePaths ProjectManagerPrivate::dependenciesOrder() const
{
    QList<QPair<FilePath, FilePaths>> unordered;
    FilePaths ordered;

    // copy the map to a temporary list
    for (const Project *pro : m_projects) {
        const FilePath proName = pro->projectFilePath();
        const FilePaths depList = filtered(m_depMap.value(proName),
                                             [this](const FilePath &proPath) {
            return contains(m_projects, [proPath](const Project *p) {
                return p->projectFilePath() == proPath;
            });
        });
        unordered.push_back({proName, depList});
    }

    while (!unordered.isEmpty()) {
        for (int i = (unordered.count() - 1); i >= 0; --i) {
            if (unordered.at(i).second.isEmpty()) {
                ordered << unordered.at(i).first;
                unordered.removeAt(i);
            }
        }

        // remove the handled projects from the dependency lists
        // of the remaining unordered projects
        for (int i = 0; i < unordered.count(); ++i) {
            for (const FilePath &pro : std::as_const(ordered)) {
                FilePaths depList = unordered.at(i).second;
                depList.removeAll(pro);
                unordered[i].second = depList;
            }
        }
    }

    return ordered;
}

QList<Project *> ProjectManager::projectOrder(const Project *project)
{
    QList<Project *> result;

    FilePaths pros;
    if (project)
        pros = d->dependencies(project->projectFilePath());
    else
        pros = d->dependenciesOrder();

    for (const FilePath &proFile : std::as_const(pros)) {
        for (Project *pro : projects()) {
            if (pro->projectFilePath() == proFile) {
                result << pro;
                break;
            }
        }
    }

    return result;
}

Project *ProjectManager::projectForFile(const FilePath &fileName)
{
    if (Project * const project = Utils::findOrDefault(ProjectManager::projects(),
            [&fileName](const Project *p) { return p->isKnownFile(fileName); })) {
        return project;
    }
    return Utils::findOrDefault(ProjectManager::projects(),
                                [&fileName](const Project *p) {
        for (const Target * const target : p->targets()) {
            for (const BuildConfiguration * const bc : target->buildConfigurations()) {
                if (fileName.isChildOf(bc->buildDirectory()))
                    return false;
            }
        }
        return fileName.isChildOf(p->projectDirectory());
    });
}

Project *ProjectManager::projectWithProjectFilePath(const FilePath &filePath)
{
    return Utils::findOrDefault(ProjectManager::projects(),
            [&filePath](const Project *p) { return p->projectFilePath() == filePath; });
}

void ProjectManager::configureEditor(IEditor *editor, const QString &fileName)
{
    if (auto textEditor = qobject_cast<TextEditor::BaseTextEditor*>(editor)) {
        Project *project = projectForFile(Utils::FilePath::fromString(fileName));
        // Global settings are the default.
        if (project)
            project->editorConfiguration()->configureEditor(textEditor);
    }
}

void ProjectManager::configureEditors(Project *project)
{
    const QList<IDocument *> documents = DocumentModel::openedDocuments();
    for (IDocument *document : documents) {
        if (project->isKnownFile(document->filePath())) {
            const QList<IEditor *> editors = DocumentModel::editorsForDocument(document);
            for (IEditor *editor : editors) {
                if (auto textEditor = qobject_cast<TextEditor::BaseTextEditor*>(editor)) {
                        project->editorConfiguration()->configureEditor(textEditor);
                }
            }
        }
    }
}

void ProjectManager::removeProjects(const QList<Project *> &remove)
{
    for (Project *pro : remove)
        emit m_instance->aboutToRemoveProject(pro);

    bool changeStartupProject = false;

    // Delete projects
    for (Project *pro : remove) {
        pro->saveSettings();
        pro->markAsShuttingDown();

        // Remove the project node:
        d->m_projects.removeOne(pro);

        if (pro == d->m_startupProject)
            changeStartupProject = true;

        FolderNavigationWidgetFactory::removeRootDirectory(projectFolderId(pro));
        disconnect(pro, nullptr, m_instance, nullptr);
        emit m_instance->projectRemoved(pro);
    }

    if (changeStartupProject)
        setStartupProject(hasProjects() ? projects().first() : nullptr);

     qDeleteAll(remove);
}

void ProjectManagerPrivate::restoreDependencies(const PersistentSettingsReader &reader)
{
    QMap<QString, QVariant> depMap = reader.restoreValue(QLatin1String("ProjectDependencies")).toMap();
    auto i = depMap.constBegin();
    while (i != depMap.constEnd()) {
        const QString &key = i.key();
        FilePaths values;
        const QStringList valueList = i.value().toStringList();
        for (const QString &value : valueList)
            values << FilePath::fromString(value);
        m_depMap.insert(FilePath::fromString(key), values);
        ++i;
    }
}

void ProjectManagerPrivate::askUserAboutFailedProjects()
{
    FilePaths failedProjects = m_failedProjects;
    if (!failedProjects.isEmpty()) {
        QString fileList = FilePath::formatFilePaths(failedProjects, "<br>");
        QMessageBox box(QMessageBox::Warning,
                                   Tr::tr("Failed to restore project files"),
                                   Tr::tr("Could not restore the following project files:<br><b>%1</b>").
                                   arg(fileList));
        auto keepButton = new QPushButton(Tr::tr("Keep projects in Session"), &box);
        auto removeButton = new QPushButton(Tr::tr("Remove projects from Session"), &box);
        box.addButton(keepButton, QMessageBox::AcceptRole);
        box.addButton(removeButton, QMessageBox::DestructiveRole);

        box.exec();

        if (box.clickedButton() == removeButton)
            m_failedProjects.clear();
    }
}

void ProjectManagerPrivate::restoreStartupProject(const PersistentSettingsReader &reader)
{
    const FilePath startupProject = FilePath::fromSettings(reader.restoreValue("StartupProject"));
    if (!startupProject.isEmpty()) {
        for (Project *pro : std::as_const(m_projects)) {
            if (pro->projectFilePath() == startupProject) {
                m_instance->setStartupProject(pro);
                break;
            }
        }
    }
    if (!m_startupProject) {
        if (!startupProject.isEmpty())
            qWarning() << "Could not find startup project" << startupProject;
        if (hasProjects())
            m_instance->setStartupProject(m_projects.first());
    }
}

/*!
     Loads a session, takes a session name (not filename).
*/
void ProjectManagerPrivate::restoreProjects(const FilePaths &fileList)
{
    // indirectly adds projects to session
    // Keep projects that failed to load in the session!
    m_failedProjects = fileList;
    if (!fileList.isEmpty()) {
        ProjectExplorerPlugin::OpenProjectResult result = ProjectExplorerPlugin::openProjects(fileList);
        if (!result)
            ProjectExplorerPlugin::showOpenProjectError(result);
        const QList<Project *> projects = result.projects();
        for (const Project *p : projects)
            m_failedProjects.removeAll(p->projectFilePath());
    }
}

/*
 * ========== Notes on storing and loading the default session ==========
 * The default session comes in two flavors: implicit and explicit. The implicit one,
 * also referred to as "default virgin" in the code base, is the one that is active
 * at start-up, if no session has been explicitly loaded due to command-line arguments
 * or the "restore last session" setting in the session manager.
 * The implicit default session silently turns into the explicit default session
 * by loading a project or a file or changing settings in the Dependencies panel. The explicit
 * default session can also be loaded by the user via the Welcome Screen.
 * This mechanism somewhat complicates the handling of session-specific settings such as
 * the ones in the task pane: Users expect that changes they make there become persistent, even
 * when they are in the implicit default session. However, we can't just blindly store
 * the implicit default session, because then we'd overwrite the project list of the explicit
 * default session. Therefore, we use the following logic:
 *     - Upon start-up, if no session is to be explicitly loaded, we restore the parts of the
 *       explicit default session that are not related to projects, editors etc; the
 *       "general settings" of the session, so to speak.
 *     - When storing the implicit default session, we overwrite only these "general settings"
 *       of the explicit default session and keep the others as they are.
 *     - When switching from the implicit to the explicit default session, we keep the
 *       "general settings" and load everything else from the session file.
 * This guarantees that user changes are properly transferred and nothing gets lost from
 * either the implicit or the explicit default session.
 *
 */
bool ProjectManager::loadSession(const QString &session, bool initial)
{
    const bool loadImplicitDefault = session.isEmpty();
    const bool switchFromImplicitToExplicitDefault = session == DEFAULT_SESSION
            && sb_d->m_sessionName == DEFAULT_SESSION && !initial;

    // Do nothing if we have that session already loaded,
    // exception if the session is the default virgin session
    // we still want to be able to load the default session
    if (session == sb_d->m_sessionName && !SessionManager::isDefaultVirgin())
        return true;

    if (!loadImplicitDefault && !SessionManager::sessions().contains(session))
        return false;

    FilePaths fileList;
    // Try loading the file
    FilePath fileName = SessionManager::sessionNameToFileName(loadImplicitDefault ? DEFAULT_SESSION : session);
    PersistentSettingsReader reader;
    if (fileName.exists()) {
        if (!reader.load(fileName)) {
            QMessageBox::warning(ICore::dialogParent(), Tr::tr("Error while restoring session"),
                                 Tr::tr("Could not restore session %1").arg(fileName.toUserOutput()));

            return false;
        }

        if (loadImplicitDefault) {
            sb_d->restoreValues(reader);
            emit SessionManager::instance()->sessionLoaded(DEFAULT_SESSION);
            return true;
        }

        fileList = FileUtils::toFilePathList(reader.restoreValue("ProjectList").toStringList());
    } else if (loadImplicitDefault) {
        return true;
    }

    sb_d->m_loadingSession = true;

    // Allow everyone to set something in the session and before saving
    emit SessionManager::instance()->aboutToUnloadSession(sb_d->m_sessionName);

    if (!save()) {
        sb_d->m_loadingSession = false;
        return false;
    }

    // Clean up
    if (!EditorManager::closeAllEditors()) {
        sb_d->m_loadingSession = false;
        return false;
    }

    // find a list of projects to close later
    const QList<Project *> projectsToRemove = Utils::filtered(projects(), [&fileList](Project *p) {
        return !fileList.contains(p->projectFilePath());
    });
    const QList<Project *> openProjects = projects();
    const FilePaths projectPathsToLoad = Utils::filtered(fileList, [&openProjects](const FilePath &path) {
        return !Utils::contains(openProjects, [&path](Project *p) {
            return p->projectFilePath() == path;
        });
    });
    d->m_failedProjects.clear();
    d->m_depMap.clear();
    if (!switchFromImplicitToExplicitDefault)
        sb_d->m_values.clear();
    d->m_casadeSetActive = false;

    sb_d->m_sessionName = session;
    delete sb_d->m_writer;
    sb_d->m_writer = nullptr;
    EditorManager::updateWindowTitles();

    if (fileName.exists()) {
        sb_d->m_virginSession = false;

        ProgressManager::addTask(sb_d->m_future.future(), Tr::tr("Loading Session"),
           "ProjectExplorer.SessionFile.Load");

        sb_d->m_future.setProgressRange(0, 1);
        sb_d->m_future.setProgressValue(0);

        if (!switchFromImplicitToExplicitDefault)
            sb_d->restoreValues(reader);
        emit SessionManager::instance()->aboutToLoadSession(session);

        // retrieve all values before the following code could change them again
        Id modeId = Id::fromSetting(SessionManager::value(QLatin1String("ActiveMode")));
        if (!modeId.isValid())
            modeId = Id(Core::Constants::MODE_EDIT);

        QColor c = QColor(reader.restoreValue(QLatin1String("Color")).toString());
        if (c.isValid())
            StyleHelper::setBaseColor(c);

        sb_d->m_future.setProgressRange(0, projectPathsToLoad.count() + 1/*initialization above*/ + 1/*editors*/);
        sb_d->m_future.setProgressValue(1);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        d->restoreProjects(projectPathsToLoad);
        sb_d->sessionLoadingProgress();
        d->restoreDependencies(reader);
        d->restoreStartupProject(reader);

        removeProjects(projectsToRemove); // only remove old projects now that the startup project is set!

        sb_d->restoreEditors(reader);

        sb_d->m_future.reportFinished();
        sb_d->m_future = QFutureInterface<void>();

        // Fall back to Project mode if the startup project is unconfigured and
        // use the mode saved in the session otherwise
        if (d->m_startupProject && d->m_startupProject->needsConfiguration())
            modeId = Id(Constants::MODE_SESSION);

        ModeManager::activateMode(modeId);
        ModeManager::setFocusToCurrentMode();
    } else {
        removeProjects(projects());
        ModeManager::activateMode(Id(Core::Constants::MODE_EDIT));
        ModeManager::setFocusToCurrentMode();
    }

    d->m_casadeSetActive = reader.restoreValue(QLatin1String("CascadeSetActive"), false).toBool();
    sb_d->m_lastActiveTimes.insert(session, QDateTime::currentDateTime());

    emit SessionManager::instance()->sessionLoaded(session);

    // Starts a event loop, better do that at the very end
    d->askUserAboutFailedProjects();
    sb_d->m_loadingSession = false;
    return true;
}

void ProjectManager::reportProjectLoadingProgress()
{
    sb_d->sessionLoadingProgress();
}

FilePaths ProjectManager::projectsForSessionName(const QString &session)
{
    const FilePath fileName = SessionManager::sessionNameToFileName(session);
    PersistentSettingsReader reader;
    if (fileName.exists()) {
        if (!reader.load(fileName)) {
            qWarning() << "Could not restore session" << fileName.toUserOutput();
            return {};
        }
    }
    return transform(reader.restoreValue(QLatin1String("ProjectList")).toStringList(),
                     &FilePath::fromUserInput);
}

#ifdef WITH_TESTS

void ProjectExplorerPlugin::testSessionSwitch()
{
    QVERIFY(SessionManager::createSession("session1"));
    QVERIFY(SessionManager::createSession("session2"));
    QTemporaryFile cppFile("main.cpp");
    QVERIFY(cppFile.open());
    cppFile.close();
    QTemporaryFile projectFile1("XXXXXX.pro");
    QTemporaryFile projectFile2("XXXXXX.pro");
    struct SessionSpec {
        SessionSpec(const QString &n, QTemporaryFile &f) : name(n), projectFile(f) {}
        const QString name;
        QTemporaryFile &projectFile;
    };
    std::vector<SessionSpec> sessionSpecs{SessionSpec("session1", projectFile1),
                SessionSpec("session2", projectFile2)};
    for (const SessionSpec &sessionSpec : sessionSpecs) {
        static const QByteArray proFileContents
                = "TEMPLATE = app\n"
                  "CONFIG -= qt\n"
                  "SOURCES = " + cppFile.fileName().toLocal8Bit();
        QVERIFY(sessionSpec.projectFile.open());
        sessionSpec.projectFile.write(proFileContents);
        sessionSpec.projectFile.close();
        QVERIFY(ProjectManager::loadSession(sessionSpec.name));
        const OpenProjectResult openResult
                = ProjectExplorerPlugin::openProject(
                    FilePath::fromString(sessionSpec.projectFile.fileName()));
        if (openResult.errorMessage().contains("text/plain"))
            QSKIP("This test requires the presence of QmakeProjectManager to be fully functional");
        QVERIFY(openResult);
        QCOMPARE(openResult.projects().count(), 1);
        QVERIFY(openResult.project());
        QCOMPARE(ProjectManager::projects().count(), 1);
    }
    for (int i = 0; i < 30; ++i) {
        QVERIFY(ProjectManager::loadSession("session1"));
        QCOMPARE(SessionManager::activeSession(), "session1");
        QCOMPARE(ProjectManager::projects().count(), 1);
        QVERIFY(ProjectManager::loadSession("session2"));
        QCOMPARE(SessionManager::activeSession(), "session2");
        QCOMPARE(ProjectManager::projects().count(), 1);
    }
    QVERIFY(ProjectManager::loadSession("session1"));
    ProjectManager::closeAllProjects();
    QVERIFY(ProjectManager::loadSession("session2"));
    ProjectManager::closeAllProjects();
    QVERIFY(SessionManager::deleteSession("session1"));
    QVERIFY(SessionManager::deleteSession("session2"));
}

#endif // WITH_TESTS

} // namespace ProjectExplorer
