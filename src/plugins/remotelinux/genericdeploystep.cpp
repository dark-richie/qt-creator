// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "genericdeploystep.h"

#include "abstractremotelinuxdeploystep.h"
#include "remotelinux_constants.h"
#include "remotelinuxtr.h"

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/deploymentdata.h>
#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/filetransfer.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kitaspects.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/process.h>
#include <utils/processinterface.h>

using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace RemoteLinux::Internal {

// RsyncDeployStep

class GenericDeployStep : public AbstractRemoteLinuxDeployStep
{
public:
    GenericDeployStep(BuildStepList *bsl, Id id)
        : AbstractRemoteLinuxDeployStep(bsl, id)
    {
        flags.setDisplayStyle(StringAspect::LineEditDisplay);
        flags.setSettingsKey("RemoteLinux.RsyncDeployStep.Flags");
        flags.setLabelText(Tr::tr("Flags for rsync:"));
        flags.setValue(FileTransferSetupData::defaultRsyncFlags());

        ignoreMissingFiles.setSettingsKey("RemoteLinux.RsyncDeployStep.IgnoreMissingFiles");
        ignoreMissingFiles.setLabelText(Tr::tr("Ignore missing files:"));
        ignoreMissingFiles.setLabelPlacement(BoolAspect::LabelPlacement::InExtraLabel);

        method.setSettingsKey("RemoteLinux.RsyncDeployStep.TransferMethod");
        method.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        method.setDisplayName(Tr::tr("Transfer method:"));
        method.addOption(Tr::tr("Use rsync if available. Otherwise use default transfer."));
        method.addOption(Tr::tr("Use sftp if available. Otherwise use default transfer."));
        method.addOption(Tr::tr("Use default transfer. This might be slow."));

        setInternalInitializer([this]() -> expected_str<void> {
            if (BuildDeviceKitAspect::device(kit()) == DeviceKitAspect::device(kit())) {
                // rsync transfer on the same device currently not implemented
                // and typically not wanted.
                return make_unexpected(
                    Tr::tr("rsync is only supported for transfers between different devices."));
            }
            return isDeploymentPossible();
        });
    }

private:
    GroupItem deployRecipe() final;
    GroupItem mkdirTask(const Storage<FilesToTransfer> &storage);
    GroupItem transferTask(const Storage<FilesToTransfer> &storage);

    StringAspect flags{this};
    BoolAspect ignoreMissingFiles{this};
    SelectionAspect method{this};
};

GroupItem GenericDeployStep::mkdirTask(const Storage<FilesToTransfer> &storage)
{
    using ResultType = expected_str<void>;

    const auto onSetup = [storage](Async<ResultType> &async) {
        FilePaths remoteDirs;
        for (const FileToTransfer &file : *storage)
            remoteDirs << file.m_target.parentDir();

        FilePath::sort(remoteDirs);
        FilePath::removeDuplicates(remoteDirs);

        async.setConcurrentCallData([remoteDirs](QPromise<ResultType> &promise) {
            for (const FilePath &dir : remoteDirs) {
                const expected_str<void> result = dir.ensureWritableDir();
                promise.addResult(result);
                if (!result)
                    promise.future().cancel();
            }
        });
    };

    const auto onError = [this](const Async<ResultType> &async) {
        const int numResults = async.future().resultCount();
        if (numResults == 0) {
            addErrorMessage(
                Tr::tr("Unknown error occurred while trying to create remote directories") + '\n');
            return;
        }

        for (int i = 0; i < numResults; ++i) {
            const ResultType result = async.future().resultAt(i);
            if (!result.has_value())
                addErrorMessage(result.error());
        }
    };

    return AsyncTask<ResultType>(onSetup, onError, CallDoneIf::Error);
}

static FileTransferMethod supportedTransferMethodFor(const FileToTransfer &fileToTransfer)
{
    auto sourceDevice = ProjectExplorer::DeviceManager::deviceForPath(fileToTransfer.m_source);
    auto targetDevice = ProjectExplorer::DeviceManager::deviceForPath(fileToTransfer.m_target);

    if (sourceDevice && targetDevice) {
        // TODO: Check if the devices can reach each other via their ip
        if (sourceDevice->extraData(ProjectExplorer::Constants::SUPPORTS_RSYNC).toBool()
            && targetDevice->extraData(ProjectExplorer::Constants::SUPPORTS_RSYNC).toBool()) {
            return FileTransferMethod::Rsync;
        }

        if (sourceDevice->extraData(ProjectExplorer::Constants::SUPPORTS_SFTP).toBool()
            && targetDevice->extraData(ProjectExplorer::Constants::SUPPORTS_SFTP).toBool()) {
            return FileTransferMethod::Sftp;
        }
    }

    return FileTransferMethod::GenericCopy;
}

GroupItem GenericDeployStep::transferTask(const Storage<FilesToTransfer> &storage)
{
    const auto onSetup = [this, storage](FileTransfer &transfer) {
        FileTransferMethod preferredTransferMethod = FileTransferMethod::Rsync;
        if (method() == 0)
            preferredTransferMethod = FileTransferMethod::Rsync;
        else if (method() == 1)
            preferredTransferMethod = FileTransferMethod::Sftp;
        else
            preferredTransferMethod = FileTransferMethod::GenericCopy;

        FileTransferMethod transferMethod = preferredTransferMethod;

        if (transferMethod != FileTransferMethod::GenericCopy) {
            for (const FileToTransfer &fileToTransfer : *storage) {
                const FileTransferMethod supportedMethod = supportedTransferMethodFor(
                    fileToTransfer);

                if (supportedMethod != preferredTransferMethod) {
                    transferMethod = FileTransferMethod::GenericCopy;
                    break;
                }
            }
        }

        transfer.setTransferMethod(transferMethod);

        transfer.setRsyncFlags(flags());
        transfer.setFilesToTransfer(*storage);
        connect(&transfer, &FileTransfer::progress, this, &GenericDeployStep::handleStdOutData);
    };
    const auto onError = [this](const FileTransfer &transfer) {
        const ProcessResultData result = transfer.resultData();
        if (result.m_error == QProcess::FailedToStart) {
            addErrorMessage(Tr::tr("rsync failed to start: %1").arg(result.m_errorString));
        } else if (result.m_exitStatus == QProcess::CrashExit) {
            addErrorMessage(Tr::tr("rsync crashed."));
        } else if (result.m_exitCode != 0) {
            addErrorMessage(Tr::tr("rsync failed with exit code %1.").arg(result.m_exitCode)
                            + "\n" + result.m_errorString);
        }
    };
    return FileTransferTask(onSetup, onError, CallDoneIf::Error);
}

GroupItem GenericDeployStep::deployRecipe()
{
    const Storage<FilesToTransfer> storage;

    const auto onSetup = [this, storage] {
        const QList<DeployableFile> deployableFiles = target()->deploymentData().allFiles();
        FilesToTransfer &files = *storage;
        for (const DeployableFile &file : deployableFiles) {
            if (!ignoreMissingFiles() || file.localFilePath().exists()) {
                const FilePermissions permissions = file.isExecutable()
                    ? FilePermissions::ForceExecutable : FilePermissions::Default;
                files.append({file.localFilePath(),
                              deviceConfiguration()->filePath(file.remoteFilePath()), permissions});
            }
        }
        if (files.isEmpty()) {
            addSkipDeploymentMessage();
            return SetupResult::StopWithSuccess;
        }
        return SetupResult::Continue;
    };

    return Group {
        storage,
        onGroupSetup(onSetup),
        mkdirTask(storage),
        transferTask(storage)
    };
}

// Factory

GenericDeployStepFactory::GenericDeployStepFactory()
{
    registerStep<GenericDeployStep>(Constants::GenericDeployStepId);
    setDisplayName(Tr::tr("Deploy files"));
}

} // RemoteLinux::Internal
