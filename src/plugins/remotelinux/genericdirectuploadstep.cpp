// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "genericdirectuploadstep.h"
#include "abstractremotelinuxdeploystep.h"

#include "remotelinux_constants.h"
#include "remotelinuxtr.h"

#include <projectexplorer/deployablefile.h>
#include <projectexplorer/deploymentdata.h>
#include <projectexplorer/devicesupport/filetransfer.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>

#include <utils/hostosinfo.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QDateTime>

using namespace ProjectExplorer;
using namespace Utils;
using namespace Utils::Tasking;

namespace RemoteLinux::Internal {

const int MaxConcurrentStatCalls = 10;

struct UploadStorage
{
    QList<DeployableFile> filesToUpload;
};

enum class IncrementalDeployment { Enabled, Disabled, NotSupported };

class GenericDirectUploadStep : public AbstractRemoteLinuxDeployStep
{
public:
    GenericDirectUploadStep(ProjectExplorer::BuildStepList *bsl, Id id)
        : AbstractRemoteLinuxDeployStep(bsl, id)
    {
        auto incremental = addAspect<BoolAspect>();
        incremental->setSettingsKey("RemoteLinux.GenericDirectUploadStep.Incremental");
        incremental->setLabel(Tr::tr("Incremental deployment"),
                              BoolAspect::LabelPlacement::AtCheckBox);
        incremental->setValue(true);
        incremental->setDefaultValue(true);

        auto ignoreMissingFiles = addAspect<BoolAspect>();
        ignoreMissingFiles->setSettingsKey("RemoteLinux.GenericDirectUploadStep.IgnoreMissingFiles");
        ignoreMissingFiles->setLabel(Tr::tr("Ignore missing files"),
                                     BoolAspect::LabelPlacement::AtCheckBox);
        ignoreMissingFiles->setValue(false);

        setInternalInitializer([this, incremental, ignoreMissingFiles] {
            m_incremental = incremental->value()
                                   ? IncrementalDeployment::Enabled : IncrementalDeployment::Disabled;
            m_ignoreMissingFiles = ignoreMissingFiles->value();
            return isDeploymentPossible();
        });

        setRunPreparer([this] {
            m_deployableFiles = target()->deploymentData().allFiles();
        });
    }

    bool isDeploymentNecessary() const final;
    Utils::Tasking::Group deployRecipe() final;

    QDateTime timestampFromStat(const DeployableFile &file, QtcProcess *statProc);

    using FilesToStat = std::function<QList<DeployableFile>(UploadStorage *)>;
    using StatEndHandler
          = std::function<void(UploadStorage *, const DeployableFile &, const QDateTime &)>;
    TaskItem statTask(UploadStorage *storage, const DeployableFile &file,
                      StatEndHandler statEndHandler);
    TaskItem statTree(const TreeStorage<UploadStorage> &storage, FilesToStat filesToStat,
                      StatEndHandler statEndHandler);
    TaskItem uploadTask(const TreeStorage<UploadStorage> &storage);
    TaskItem chmodTask(const DeployableFile &file);
    TaskItem chmodTree(const TreeStorage<UploadStorage> &storage);

    IncrementalDeployment m_incremental = IncrementalDeployment::NotSupported;
    bool m_ignoreMissingFiles = false;
    mutable QList<DeployableFile> m_deployableFiles;
};

static QList<DeployableFile> collectFilesToUpload(const DeployableFile &deployable)
{
    QList<DeployableFile> collected;
    FilePath localFile = deployable.localFilePath();
    if (localFile.isDir()) {
        const FilePaths files = localFile.dirEntries(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        const QString remoteDir = deployable.remoteDirectory() + '/' + localFile.fileName();
        for (const FilePath &localFilePath : files)
            collected.append(collectFilesToUpload(DeployableFile(localFilePath, remoteDir)));
    } else {
        collected << deployable;
    }
    return collected;
}

bool GenericDirectUploadStep::isDeploymentNecessary() const
{
    QList<DeployableFile> collected;
    for (int i = 0; i < m_deployableFiles.count(); ++i)
        collected.append(collectFilesToUpload(m_deployableFiles.at(i)));

    QTC_CHECK(collected.size() >= m_deployableFiles.size());
    m_deployableFiles = collected;
    return !m_deployableFiles.isEmpty();
}

QDateTime GenericDirectUploadStep::timestampFromStat(const DeployableFile &file,
                                                     QtcProcess *statProc)
{
    bool succeeded = false;
    QString error;
    if (statProc->error() == QProcess::FailedToStart) {
        error = Tr::tr("Failed to start \"stat\": %1").arg(statProc->errorString());
    } else if (statProc->exitStatus() == QProcess::CrashExit) {
        error = Tr::tr("\"stat\" crashed.");
    } else if (statProc->exitCode() != 0) {
        error = Tr::tr("\"stat\" failed with exit code %1: %2")
                .arg(statProc->exitCode()).arg(statProc->cleanedStdErr());
    } else {
        succeeded = true;
    }
    if (!succeeded) {
        addWarningMessage(Tr::tr("Failed to retrieve remote timestamp for file \"%1\". "
                                 "Incremental deployment will not work. Error message was: %2")
                              .arg(file.remoteFilePath(), error));
        return {};
    }
    const QByteArray output = statProc->readAllRawStandardOutput().trimmed();
    const QString warningString(Tr::tr("Unexpected stat output for remote file \"%1\": %2")
                                .arg(file.remoteFilePath()).arg(QString::fromUtf8(output)));
    if (!output.startsWith(file.remoteFilePath().toUtf8())) {
        addWarningMessage(warningString);
        return {};
    }
    const QByteArrayList columns = output.mid(file.remoteFilePath().toUtf8().size() + 1).split(' ');
    if (columns.size() < 14) { // Normal Linux stat: 16 columns in total, busybox stat: 15 columns
        addWarningMessage(warningString);
        return {};
    }
    bool isNumber;
    const qint64 secsSinceEpoch = columns.at(11).toLongLong(&isNumber);
    if (!isNumber) {
        addWarningMessage(warningString);
        return {};
    }
    return QDateTime::fromSecsSinceEpoch(secsSinceEpoch);
}

TaskItem GenericDirectUploadStep::statTask(UploadStorage *storage,
                                           const DeployableFile &file,
                                           StatEndHandler statEndHandler)
{
    const auto setupHandler = [=](QtcProcess &process) {
        // We'd like to use --format=%Y, but it's not supported by busybox.
        process.setCommand({deviceConfiguration()->filePath("stat"),
                            {"-t", Utils::ProcessArgs::quoteArgUnix(file.remoteFilePath())}});
    };
    const auto endHandler = [=](const QtcProcess &process) {
        QtcProcess *proc = const_cast<QtcProcess *>(&process);
        const QDateTime timestamp = timestampFromStat(file, proc);
        statEndHandler(storage, file, timestamp);
    };
    return Process(setupHandler, endHandler, endHandler);
}

TaskItem GenericDirectUploadStep::statTree(const TreeStorage<UploadStorage> &storage,
                                           FilesToStat filesToStat, StatEndHandler statEndHandler)
{
    const auto setupHandler = [=](TaskTree &tree) {
        UploadStorage *storagePtr = storage.activeStorage();
        const QList<DeployableFile> files = filesToStat(storagePtr);
        QList<TaskItem> statList{optional, ParallelLimit(MaxConcurrentStatCalls)};
        for (const DeployableFile &file : std::as_const(files)) {
            QTC_ASSERT(file.isValid(), continue);
            statList.append(statTask(storagePtr, file, statEndHandler));
        }
        tree.setupRoot({statList});
    };
    return Tree(setupHandler);
}

TaskItem GenericDirectUploadStep::uploadTask(const TreeStorage<UploadStorage> &storage)
{
    const auto setupHandler = [this, storage](FileTransfer &transfer) {
        if (storage->filesToUpload.isEmpty()) {
            addProgressMessage(Tr::tr("No files need to be uploaded."));
            return TaskAction::StopWithDone;
        }
        addProgressMessage(Tr::tr("%n file(s) need to be uploaded.", "",
                                  storage->filesToUpload.size()));
        FilesToTransfer files;
        for (const DeployableFile &file : std::as_const(storage->filesToUpload)) {
            if (!file.localFilePath().exists()) {
                const QString message = Tr::tr("Local file \"%1\" does not exist.")
                                              .arg(file.localFilePath().toUserOutput());
                if (m_ignoreMissingFiles) {
                    addWarningMessage(message);
                    continue;
                }
                addErrorMessage(message);
                return TaskAction::StopWithError;
            }
            files.append({file.localFilePath(),
                          deviceConfiguration()->filePath(file.remoteFilePath())});
        }
        if (files.isEmpty()) {
            addProgressMessage(Tr::tr("No files need to be uploaded."));
            return TaskAction::StopWithDone;
        }
        transfer.setFilesToTransfer(files);
        QObject::connect(&transfer, &FileTransfer::progress,
                         this, &GenericDirectUploadStep::addProgressMessage);
        return TaskAction::Continue;
    };
    const auto errorHandler = [this](const FileTransfer &transfer) {
        addErrorMessage(transfer.resultData().m_errorString);
    };

    return Transfer(setupHandler, {}, errorHandler);
}

TaskItem GenericDirectUploadStep::chmodTask(const DeployableFile &file)
{
    const auto setupHandler = [=](QtcProcess &process) {
        process.setCommand({deviceConfiguration()->filePath("chmod"),
                {"a+x", Utils::ProcessArgs::quoteArgUnix(file.remoteFilePath())}});
    };
    const auto errorHandler = [=](const QtcProcess &process) {
        const QString error = process.errorString();
        if (!error.isEmpty()) {
            addWarningMessage(Tr::tr("Remote chmod failed for file \"%1\": %2")
                                  .arg(file.remoteFilePath(), error));
        } else if (process.exitCode() != 0) {
            addWarningMessage(Tr::tr("Remote chmod failed for file \"%1\": %2")
                                  .arg(file.remoteFilePath(), process.cleanedStdErr()));
        }
    };
    return Process(setupHandler, {}, errorHandler);
}

TaskItem GenericDirectUploadStep::chmodTree(const TreeStorage<UploadStorage> &storage)
{
    const auto setupChmodHandler = [=](TaskTree &tree) {
        QList<DeployableFile> filesToChmod;
        for (const DeployableFile &file : std::as_const(storage->filesToUpload)) {
            if (file.isExecutable())
                filesToChmod << file;
        }
        QList<TaskItem> chmodList{optional, ParallelLimit(MaxConcurrentStatCalls)};
        for (const DeployableFile &file : std::as_const(filesToChmod)) {
            QTC_ASSERT(file.isValid(), continue);
            chmodList.append(chmodTask(file));
        }
        tree.setupRoot({chmodList});
    };
    return Tree(setupChmodHandler);
}

Group GenericDirectUploadStep::deployRecipe()
{
    const auto preFilesToStat = [this](UploadStorage *storage) {
        QList<DeployableFile> filesToStat;
        for (const DeployableFile &file : std::as_const(m_deployableFiles)) {
            if (m_incremental != IncrementalDeployment::Enabled || hasLocalFileChanged(file)) {
                storage->filesToUpload.append(file);
                continue;
            }
            if (m_incremental == IncrementalDeployment::NotSupported)
                continue;
            filesToStat << file;
        }
        return filesToStat;
    };
    const auto preStatEndHandler = [this](UploadStorage *storage, const DeployableFile &file,
                                          const QDateTime &timestamp) {
        if (!timestamp.isValid() || hasRemoteFileChanged(file, timestamp))
            storage->filesToUpload.append(file);
    };

    const auto postFilesToStat = [this](UploadStorage *storage) {
        return m_incremental == IncrementalDeployment::NotSupported
               ? QList<DeployableFile>() : storage->filesToUpload;
    };
    const auto postStatEndHandler = [this](UploadStorage *storage, const DeployableFile &file,
                                           const QDateTime &timestamp) {
        Q_UNUSED(storage)
        if (timestamp.isValid())
            saveDeploymentTimeStamp(file, timestamp);
    };
    const auto doneHandler = [this] {
        addProgressMessage(Tr::tr("All files successfully deployed."));
    };

    const TreeStorage<UploadStorage> storage;
    const Group root {
        Storage(storage),
        statTree(storage, preFilesToStat, preStatEndHandler),
        uploadTask(storage),
        Group {
            chmodTree(storage),
            statTree(storage, postFilesToStat, postStatEndHandler)
        },
        OnGroupDone(doneHandler)
    };
    return root;
}

// Factory

GenericDirectUploadStepFactory::GenericDirectUploadStepFactory()
{
    registerStep<GenericDirectUploadStep>(Constants::DirectUploadStepId);
    setDisplayName(Tr::tr("Upload files via SFTP"));
}

} // RemoteLinux::Internal
