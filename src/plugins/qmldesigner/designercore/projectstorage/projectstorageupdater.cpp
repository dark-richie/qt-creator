// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectstorageupdater.h"

#include "filestatuscache.h"
#include "filesysteminterface.h"
#include "projectstorage.h"
#include "projectstoragepathwatcherinterface.h"
#include "qmldocumentparserinterface.h"
#include "qmltypesparserinterface.h"
#include "sourcepath.h"
#include "sourcepathcache.h"

#include <sqlitedatabase.h>

#include <algorithm>
#include <functional>

namespace QmlDesigner {
namespace {

QStringList filterMultipleEntries(QStringList qmlTypes)
{
    std::sort(qmlTypes.begin(), qmlTypes.end());
    qmlTypes.erase(std::unique(qmlTypes.begin(), qmlTypes.end()), qmlTypes.end());

    return qmlTypes;
}

QList<QmlDirParser::Import> filterMultipleEntries(QList<QmlDirParser::Import> imports)
{
    std::stable_sort(imports.begin(), imports.end(), [](auto &&first, auto &&second) {
        return first.module < second.module;
    });
    imports.erase(std::unique(imports.begin(),
                              imports.end(),
                              [](auto &&first, auto &&second) {
                                  return first.module == second.module;
                              }),
                  imports.end());

    return imports;
}

QList<QmlDirParser::Import> joinImports(const QList<QmlDirParser::Import> &firstImports,
                                        const QList<QmlDirParser::Import> &secondImports)
{
    QList<QmlDirParser::Import> imports;
    imports.reserve(firstImports.size() + secondImports.size());
    imports.append(firstImports);
    imports.append(secondImports);
    imports = filterMultipleEntries(std::move(imports));

    return imports;
}

ProjectStorageUpdater::Components createComponents(
    const QMultiHash<QString, QmlDirParser::Component> &qmlDirParserComponents,
    ModuleId moduleId,
    ModuleId pathModuleId,
    FileSystemInterface &fileSystem,
    const Utils::PathString &directory)
{
    ProjectStorageUpdater::Components components;

    auto qmlFileNames = fileSystem.qmlFileNames(QString{directory});

    components.reserve(static_cast<std::size_t>(qmlDirParserComponents.size() + qmlFileNames.size()));

    for (const QString &qmlFileName : qmlFileNames) {
        Utils::PathString fileName{qmlFileName};
        Utils::PathString typeName{fileName.begin(), std::find(fileName.begin(), fileName.end(), '.')};
        components.push_back(
            ProjectStorageUpdater::Component{fileName, typeName, pathModuleId, -1, -1});
    }

    for (const QmlDirParser::Component &qmlDirParserComponent : qmlDirParserComponents) {
        components.push_back(ProjectStorageUpdater::Component{qmlDirParserComponent.fileName,
                                                              qmlDirParserComponent.typeName,
                                                              moduleId,
                                                              qmlDirParserComponent.majorVersion,
                                                              qmlDirParserComponent.minorVersion});
    }

    return components;
}

SourceIds filterNotUpdatedSourceIds(SourceIds updatedSourceIds, SourceIds notUpdatedSourceIds)
{
    std::sort(updatedSourceIds.begin(), updatedSourceIds.end());
    std::sort(notUpdatedSourceIds.begin(), notUpdatedSourceIds.end());

    SourceIds filteredUpdatedSourceIds;
    filteredUpdatedSourceIds.reserve(updatedSourceIds.size());

    std::set_difference(updatedSourceIds.cbegin(),
                        updatedSourceIds.cend(),
                        notUpdatedSourceIds.cbegin(),
                        notUpdatedSourceIds.cend(),
                        std::back_inserter(filteredUpdatedSourceIds));

    filteredUpdatedSourceIds.erase(std::unique(filteredUpdatedSourceIds.begin(),
                                               filteredUpdatedSourceIds.end()),
                                   filteredUpdatedSourceIds.end());

    return filteredUpdatedSourceIds;
}

void addSourceIds(SourceIds &sourceIds, const Storage::Synchronization::ProjectDatas &projectDatas)
{
    for (const auto &projectData : projectDatas)
        sourceIds.push_back(projectData.sourceId);
}

Storage::Synchronization::Version convertVersion(LanguageUtils::ComponentVersion version)
{
    return Storage::Synchronization::Version{version.majorVersion(), version.minorVersion()};
}

Storage::Synchronization::IsAutoVersion convertToIsAutoVersion(QmlDirParser::Import::Flags flags)
{
    if (flags & QmlDirParser::Import::Flag::Auto)
        return Storage::Synchronization::IsAutoVersion::Yes;
    return Storage::Synchronization::IsAutoVersion::No;
}

void addDependencies(Storage::Synchronization::Imports &dependencies,
                     SourceId sourceId,
                     const QList<QmlDirParser::Import> &qmldirDependencies,
                     ProjectStorageInterface &projectStorage)
{
    for (const QmlDirParser::Import &qmldirDependency : qmldirDependencies) {
        ModuleId moduleId = projectStorage.moduleId(Utils::PathString{qmldirDependency.module}
                                                    + "-cppnative");
        dependencies.emplace_back(moduleId, Storage::Synchronization::Version{}, sourceId);
    }
}

void addModuleExportedImports(Storage::Synchronization::ModuleExportedImports &imports,
                              ModuleId moduleId,
                              ModuleId cppModuleId,
                              const QList<QmlDirParser::Import> &qmldirImports,
                              ProjectStorageInterface &projectStorage)
{
    for (const QmlDirParser::Import &qmldirImport : qmldirImports) {
        ModuleId exportedModuleId = projectStorage.moduleId(Utils::PathString{qmldirImport.module});
        imports.emplace_back(moduleId,
                             exportedModuleId,
                             convertVersion(qmldirImport.version),
                             convertToIsAutoVersion(qmldirImport.flags));

        ModuleId exportedCppModuleId = projectStorage.moduleId(
            Utils::PathString{qmldirImport.module} + "-cppnative");
        imports.emplace_back(cppModuleId,
                             exportedCppModuleId,
                             Storage::Synchronization::Version{},
                             Storage::Synchronization::IsAutoVersion::No);
    }
}

} // namespace

void ProjectStorageUpdater::update(QStringList directories,
                                   QStringList qmlTypesPaths,
                                   ProjectPartId projectPartId)
{
    Storage::Synchronization::SynchronizationPackage package;
    SourceIdsData sourceIdsData{static_cast<std::size_t>(directories.size())};
    std::vector<IdPaths> idPaths;
    idPaths.reserve(4);

    updateDirectories(directories, package, sourceIdsData);
    updateQmlTypes(qmlTypesPaths, package, sourceIdsData);

    package.updatedSourceIds = filterNotUpdatedSourceIds(std::move(package.updatedSourceIds),
                                                         std::move(sourceIdsData.notUpdatedSourceIds));
    package.updatedFileStatusSourceIds = filterNotUpdatedSourceIds(
        std::move(package.updatedFileStatusSourceIds),
        std::move(sourceIdsData.notUpdatedFileStatusSourceIds));

    m_projectStorage.synchronize(std::move(package));

    idPaths.push_back(
        {projectPartId, SourceType::Directory, std::move(sourceIdsData.watchedDirectorySourceIds)});
    idPaths.push_back(
        {projectPartId, SourceType::QmlDir, std::move(sourceIdsData.watchedQmldirSourceIds)});
    idPaths.push_back({projectPartId, SourceType::Qml, std::move(sourceIdsData.watchedQmlSourceIds)});
    idPaths.push_back(
        {projectPartId, SourceType::QmlTypes, std::move(sourceIdsData.watchedQmltypesSourceIds)});
    m_pathWatcher.updateIdPaths(idPaths);
}

void ProjectStorageUpdater::updateQmlTypes(const QStringList &qmlTypesPaths,
                                           Storage::Synchronization::SynchronizationPackage &package,
                                           SourceIdsData &sourceIdsData)
{
    if (qmlTypesPaths.empty())
        return;

    ModuleId moduleId = m_projectStorage.moduleId("QML-cppnative");

    for (const QString &qmlTypesPath : qmlTypesPaths) {
        SourceId sourceId = m_pathCache.sourceId(SourcePath{qmlTypesPath});
        sourceIdsData.watchedQmltypesSourceIds.push_back(sourceId);

        Storage::Synchronization::ProjectData projectData{sourceId,
                                                          sourceId,
                                                          moduleId,
                                                          Storage::Synchronization::FileType::QmlTypes};

        FileState state = parseTypeInfo(projectData,
                                        Utils::PathString{qmlTypesPath},
                                        package,
                                        sourceIdsData);

        if (state == FileState::Changed)
            package.projectDatas.push_back(std::move(projectData));
    }
}

namespace {
template<typename... FileStates>
ProjectStorageUpdater::FileState combineState(FileStates... fileStates)
{
    if (((fileStates == ProjectStorageUpdater::FileState::Changed) || ...))
        return ProjectStorageUpdater::FileState::Changed;

    if (((fileStates == ProjectStorageUpdater::FileState::NotChanged) || ...))
        return ProjectStorageUpdater::FileState::NotChanged;

    return ProjectStorageUpdater::FileState::NotExists;
}
} // namespace

void ProjectStorageUpdater::updateDirectories(const QStringList &directories,
                                              Storage::Synchronization::SynchronizationPackage &package,
                                              SourceIdsData &sourceIdsData)
{
    for (const QString &directory : directories)
        updateDirectory({directory}, package, sourceIdsData);
}

void ProjectStorageUpdater::updateDirectory(const Utils::PathString &directoryPath,
                                            Storage::Synchronization::SynchronizationPackage &package,
                                            SourceIdsData &sourceIdsData)
{
    SourcePath qmldirSourcePath{directoryPath + "/qmldir"};
    auto [directoryId, qmlDirSourceId] = m_pathCache.sourceContextAndSourceId(qmldirSourcePath);

    SourcePath directorySourcePath{directoryPath + "/."};
    auto directorySourceId = m_pathCache.sourceId(directorySourcePath);
    auto directoryState = fileState(directorySourceId, package, sourceIdsData);
    if (directoryState != FileState::NotExists)
        sourceIdsData.watchedDirectorySourceIds.push_back(directorySourceId);

    auto qmldirState = fileState(qmlDirSourceId, package, sourceIdsData);
    if (qmldirState != FileState::NotExists)
        sourceIdsData.watchedQmldirSourceIds.push_back(qmlDirSourceId);

    switch (combineState(directoryState, qmldirState)) {
    case FileState::Changed: {
        QmlDirParser parser;
        if (qmldirState != FileState::NotExists)
            parser.parse(m_fileSystem.contentAsQString(QString{qmldirSourcePath}));

        if (qmldirState != FileState::NotChanged)
            package.updatedSourceIds.push_back(qmlDirSourceId);

        Utils::PathString moduleName{parser.typeNamespace()};
        ModuleId moduleId = m_projectStorage.moduleId(moduleName);
        ModuleId cppModuleId = m_projectStorage.moduleId(moduleName + "-cppnative");
        ModuleId pathModuleId = m_projectStorage.moduleId(directoryPath);

        auto imports = filterMultipleEntries(parser.imports());

        addModuleExportedImports(package.moduleExportedImports,
                                 moduleId,
                                 cppModuleId,
                                 imports,
                                 m_projectStorage);
        package.updatedModuleIds.push_back(moduleId);

        const auto qmlProjectDatas = m_projectStorage.fetchProjectDatas(directorySourceId);
        addSourceIds(package.updatedSourceIds, qmlProjectDatas);
        addSourceIds(package.updatedFileStatusSourceIds, qmlProjectDatas);

        auto qmlTypes = filterMultipleEntries(parser.typeInfos());

        if (!qmlTypes.isEmpty()) {
            parseTypeInfos(qmlTypes,
                           filterMultipleEntries(parser.dependencies()),
                           imports,
                           directorySourceId,
                           directoryPath,
                           cppModuleId,
                           package,
                           sourceIdsData);
        }
        parseQmlComponents(
            createComponents(parser.components(), moduleId, pathModuleId, m_fileSystem, directoryPath),
            directorySourceId,
            directoryId,
            package,
            sourceIdsData,
            qmldirState);
        package.updatedProjectSourceIds.push_back(directorySourceId);
        break;
    }
    case FileState::NotChanged: {
        parseProjectDatas(m_projectStorage.fetchProjectDatas(directorySourceId), package, sourceIdsData);
        break;
    }
    case FileState::NotExists: {
        package.updatedFileStatusSourceIds.push_back(directorySourceId);
        package.updatedFileStatusSourceIds.push_back(qmlDirSourceId);
        package.updatedProjectSourceIds.push_back(directorySourceId);
        package.updatedSourceIds.push_back(qmlDirSourceId);
        auto qmlProjectDatas = m_projectStorage.fetchProjectDatas(directorySourceId);
        for (const Storage::Synchronization::ProjectData &projectData : qmlProjectDatas) {
            package.updatedSourceIds.push_back(projectData.sourceId);
            package.updatedFileStatusSourceIds.push_back(projectData.sourceId);
        }

        break;
    }
    }
}

void ProjectStorageUpdater::pathsWithIdsChanged([[maybe_unused]] const std::vector<IdPaths> &) {}

void ProjectStorageUpdater::pathsChanged(const SourceIds &) {}

void ProjectStorageUpdater::parseTypeInfos(const QStringList &typeInfos,
                                           const QList<QmlDirParser::Import> &qmldirDependencies,
                                           const QList<QmlDirParser::Import> &qmldirImports,
                                           SourceId directorySourceId,
                                           Utils::SmallStringView directoryPath,
                                           ModuleId moduleId,
                                           Storage::Synchronization::SynchronizationPackage &package,
                                           SourceIdsData &sourceIdData)
{
    for (const QString &typeInfo : typeInfos) {
        Utils::PathString qmltypesPath = Utils::PathString::join(
            {directoryPath, "/", Utils::SmallString{typeInfo}});
        SourceId sourceId = m_pathCache.sourceId(SourcePathView{qmltypesPath});

        sourceIdData.watchedQmltypesSourceIds.push_back(sourceId);

        addDependencies(package.moduleDependencies,
                        sourceId,
                        joinImports(qmldirDependencies, qmldirImports),
                        m_projectStorage);
        package.updatedModuleDependencySourceIds.push_back(sourceId);

        auto projectData = package.projectDatas.emplace_back(
            directorySourceId, sourceId, moduleId, Storage::Synchronization::FileType::QmlTypes);

        parseTypeInfo(projectData, qmltypesPath, package, sourceIdData);
    }
}

void ProjectStorageUpdater::parseProjectDatas(const Storage::Synchronization::ProjectDatas &projectDatas,
                                              Storage::Synchronization::SynchronizationPackage &package,
                                              SourceIdsData &sourceIdData)
{
    for (const Storage::Synchronization::ProjectData &projectData : projectDatas) {
        switch (projectData.fileType) {
        case Storage::Synchronization::FileType::QmlTypes: {
            sourceIdData.watchedQmltypesSourceIds.push_back(projectData.sourceId);

            auto qmltypesPath = m_pathCache.sourcePath(projectData.sourceId);
            parseTypeInfo(projectData, qmltypesPath, package, sourceIdData);
            break;
        }
        case Storage::Synchronization::FileType::QmlDocument: {
            sourceIdData.watchedQmlSourceIds.push_back(projectData.sourceId);

            parseQmlComponent(projectData.sourceId, package, sourceIdData);
        }
        };
    }
}

auto ProjectStorageUpdater::parseTypeInfo(const Storage::Synchronization::ProjectData &projectData,
                                          Utils::SmallStringView qmltypesPath,
                                          Storage::Synchronization::SynchronizationPackage &package,
                                          SourceIdsData &sourceIdData) -> FileState
{
    auto state = fileState(projectData.sourceId, package, sourceIdData);
    switch (state) {
    case FileState::Changed: {
        package.updatedSourceIds.push_back(projectData.sourceId);

        const auto content = m_fileSystem.contentAsQString(QString{qmltypesPath});
        m_qmlTypesParser.parse(content, package.imports, package.types, projectData);
        break;
    }
    case FileState::NotChanged: {
        sourceIdData.notUpdatedSourceIds.push_back(projectData.sourceId);
        break;
    }
    case FileState::NotExists:
        throw CannotParseQmlTypesFile{};
        break;
    }

    return state;
}

void ProjectStorageUpdater::parseQmlComponent(Utils::SmallStringView relativeFilePath,
                                              Utils::SmallStringView directoryPath,
                                              Storage::Synchronization::ExportedTypes exportedTypes,
                                              SourceId directorySourceId,
                                              Storage::Synchronization::SynchronizationPackage &package,
                                              SourceIdsData &sourceIdData,
                                              FileState qmldirState)
{
    if (std::find(relativeFilePath.begin(), relativeFilePath.end(), '+') != relativeFilePath.end())
        return;

    Utils::PathString qmlFilePath = Utils::PathString::join({directoryPath, "/", relativeFilePath});
    SourceId sourceId = m_pathCache.sourceId(SourcePathView{qmlFilePath});

    Storage::Synchronization::Type type;
    auto state = fileState(sourceId, package, sourceIdData);

    sourceIdData.watchedQmlSourceIds.push_back(sourceId);

    switch (state) {
    case FileState::NotChanged:
        if (qmldirState == FileState::NotExists) {
            sourceIdData.notUpdatedSourceIds.emplace_back(sourceId);
            package.projectDatas.emplace_back(directorySourceId,
                                              sourceId,
                                              ModuleId{},
                                              Storage::Synchronization::FileType::QmlDocument);

            return;
        }
        type.changeLevel = Storage::Synchronization::ChangeLevel::Minimal;
        break;
    case FileState::NotExists:
        throw CannotParseQmlDocumentFile{};
    case FileState::Changed:
        const auto content = m_fileSystem.contentAsQString(QString{qmlFilePath});
        type = m_qmlDocumentParser.parse(content, package.imports, sourceId, directoryPath);
        break;
    }

    package.projectDatas.emplace_back(directorySourceId,
                                      sourceId,
                                      ModuleId{},
                                      Storage::Synchronization::FileType::QmlDocument);

    package.updatedSourceIds.push_back(sourceId);

    type.typeName = SourcePath{qmlFilePath}.name();
    type.traits = Storage::TypeTraits::Reference;
    type.sourceId = sourceId;
    type.exportedTypes = std::move(exportedTypes);

    package.types.push_back(std::move(type));
}

void ProjectStorageUpdater::parseQmlComponent(SourceId sourceId,
                                              Storage::Synchronization::SynchronizationPackage &package,
                                              SourceIdsData &sourceIdData)
{
    auto state = fileState(sourceId, package, sourceIdData);
    if (state != FileState::Changed)
        return;

    package.updatedSourceIds.push_back(sourceId);

    SourcePath sourcePath = m_pathCache.sourcePath(sourceId);

    const auto content = m_fileSystem.contentAsQString(QString{sourcePath});
    auto type = m_qmlDocumentParser.parse(content, package.imports, sourceId, sourcePath.directory());

    type.typeName = sourcePath.name();
    type.traits = Storage::TypeTraits::Reference;
    type.sourceId = sourceId;
    type.changeLevel = Storage::Synchronization::ChangeLevel::ExcludeExportedTypes;

    package.types.push_back(std::move(type));
}

namespace {

template<typename Callback>
void rangeForTheSameFileName(const ProjectStorageUpdater::Components &components, Callback callback)
{
    auto current = components.begin();
    const auto end = components.end();

    while (current != end) {
        auto nextType = std::find_if(current, end, [&](const auto &component) {
            return component.fileName != current->fileName;
        });

        callback(ProjectStorageUpdater::ComponentRange{current, nextType});

        current = nextType;
    }
}

Storage::Synchronization::ExportedTypes createExportedTypes(ProjectStorageUpdater::ComponentRange components)
{
    Storage::Synchronization::ExportedTypes exportedTypes;
    exportedTypes.reserve(components.size() + 1);

    for (const ProjectStorageUpdater::Component &component : components) {
        exportedTypes.emplace_back(component.moduleId,
                                   Utils::SmallString{component.typeName},
                                   Storage::Synchronization::Version{component.majorVersion,
                                                                     component.minorVersion});
    }

    return exportedTypes;
}

} // namespace

void ProjectStorageUpdater::parseQmlComponents(Components components,
                                               SourceId directorySourceId,
                                               SourceContextId directoryId,
                                               Storage::Synchronization::SynchronizationPackage &package,
                                               SourceIdsData &sourceIdsData,
                                               FileState qmldirState)
{
    std::sort(components.begin(), components.end(), [](auto &&first, auto &&second) {
        return first.fileName < second.fileName;
    });

    auto directoryPath = m_pathCache.sourceContextPath(directoryId);

    auto callback = [&](ComponentRange componentsWithSameFileName) {
        const auto &firstComponent = *componentsWithSameFileName.begin();
        const Utils::SmallString fileName{firstComponent.fileName};
        parseQmlComponent(fileName,
                          directoryPath,
                          createExportedTypes(componentsWithSameFileName),
                          directorySourceId,
                          package,
                          sourceIdsData,
                          qmldirState);
    };

    rangeForTheSameFileName(components, callback);
}

ProjectStorageUpdater::FileState ProjectStorageUpdater::fileState(
    SourceId sourceId,
    Storage::Synchronization::SynchronizationPackage &package,
    SourceIdsData &sourceIdData) const
{
    auto currentFileStatus = m_fileStatusCache.find(sourceId);

    if (!currentFileStatus.isValid()) {
        package.updatedFileStatusSourceIds.push_back(sourceId);
        return FileState::NotExists;
    }

    auto projectStorageFileStatus = m_projectStorage.fetchFileStatus(sourceId);

    if (!projectStorageFileStatus.isValid() || projectStorageFileStatus != currentFileStatus) {
        package.fileStatuses.push_back(currentFileStatus);
        package.updatedFileStatusSourceIds.push_back(sourceId);
        return FileState::Changed;
    }

    sourceIdData.notUpdatedFileStatusSourceIds.push_back(sourceId);
    return FileState::NotChanged;
}

} // namespace QmlDesigner
