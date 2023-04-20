// Copyright (C) 2016 BlackBerry Limited. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qnxdevice.h"

#include "qnxconstants.h"
#include "qnxdeployqtlibrariesdialog.h"
#include "qnxdevicetester.h"
#include "qnxdeviceprocesslist.h"
#include "qnxdeviceprocesssignaloperation.h"
#include "qnxdevicewizard.h"
#include "qnxtr.h"

#include <remotelinux/sshprocessinterface.h>

#include <utils/port.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QRegularExpression>

using namespace ProjectExplorer;
using namespace RemoteLinux;
using namespace Utils;

namespace Qnx::Internal {

class QnxProcessImpl final : public SshProcessInterface
{
public:
    QnxProcessImpl(const LinuxDevice *linuxDevice);
    ~QnxProcessImpl() { killIfRunning(); }

private:
    QString fullCommandLine(const CommandLine &commandLine) const final;
    void handleSendControlSignal(Utils::ControlSignal controlSignal) final;

    const QString m_pidFile;
};

static std::atomic_int s_pidFileCounter = 1;

QnxProcessImpl::QnxProcessImpl(const LinuxDevice *linuxDevice)
    : SshProcessInterface(linuxDevice)
    , m_pidFile(QString("%1/qtc.%2.pid").arg(Constants::QNX_TMP_DIR).arg(s_pidFileCounter.fetch_add(1)))
{
}

QString QnxProcessImpl::fullCommandLine(const CommandLine &commandLine) const
{
    QStringList args = ProcessArgs::splitArgs(commandLine.arguments());
    args.prepend(commandLine.executable().toString());
    const QString cmd = ProcessArgs::createUnixArgs(args).toString();

    QString fullCommandLine =
        "test -f /etc/profile && . /etc/profile ; "
        "test -f $HOME/profile && . $HOME/profile ; ";

    if (!m_setup.m_workingDirectory.isEmpty())
        fullCommandLine += QString::fromLatin1("cd %1 ; ").arg(
            ProcessArgs::quoteArg(m_setup.m_workingDirectory.toString()));

    const Environment env = m_setup.m_environment;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        fullCommandLine += QString::fromLatin1("%1='%2' ")
                .arg(env.key(it)).arg(env.expandedValueForKey(env.key(it)));
    }

    fullCommandLine += QString::fromLatin1("%1 & echo $! > %2").arg(cmd).arg(m_pidFile);

    return fullCommandLine;
}

void QnxProcessImpl::handleSendControlSignal(Utils::ControlSignal controlSignal)
{
    QTC_ASSERT(controlSignal != ControlSignal::KickOff, return);
    const QString args = QString::fromLatin1("-%1 `cat %2`")
            .arg(controlSignalToInt(controlSignal)).arg(m_pidFile);
    CommandLine command = { "kill", args, CommandLine::Raw };
    // Note: This blocking call takes up to 2 ms for local remote.
    runInShell(command);
}

const char QnxVersionKey[] = "QnxVersion";

QnxDevice::QnxDevice()
{
    setDisplayType(Tr::tr("QNX"));
    setDefaultDisplayName(Tr::tr("QNX Device"));
    setOsType(OsTypeOtherUnix);

    addDeviceAction({Tr::tr("Deploy Qt libraries..."), [](const IDevice::Ptr &device, QWidget *parent) {
        QnxDeployQtLibrariesDialog dialog(device, parent);
        dialog.exec();
    }});
}

int QnxDevice::qnxVersion() const
{
    if (m_versionNumber == 0)
        updateVersionNumber();

    return m_versionNumber;
}

void QnxDevice::updateVersionNumber() const
{
    QtcProcess versionNumberProcess;

    versionNumberProcess.setCommand({filePath("uname"), {"-r"}});
    versionNumberProcess.runBlocking(EventLoopMode::On);

    QByteArray output = versionNumberProcess.readAllRawStandardOutput();
    QString versionMessage = QString::fromLatin1(output);
    const QRegularExpression versionNumberRegExp("(\\d+)\\.(\\d+)\\.(\\d+)");
    const QRegularExpressionMatch match = versionNumberRegExp.match(versionMessage);
    if (match.hasMatch()) {
        int major = match.captured(1).toInt();
        int minor = match.captured(2).toInt();
        int patch = match.captured(3).toInt();
        m_versionNumber = (major << 16)|(minor<<8)|(patch);
    }
}

void QnxDevice::fromMap(const QVariantMap &map)
{
    m_versionNumber = map.value(QLatin1String(QnxVersionKey), 0).toInt();
    LinuxDevice::fromMap(map);
}

QVariantMap QnxDevice::toMap() const
{
    QVariantMap map(LinuxDevice::toMap());
    map.insert(QLatin1String(QnxVersionKey), m_versionNumber);
    return map;
}

PortsGatheringMethod QnxDevice::portsGatheringMethod() const
{
    return {
        // TODO: The command is probably needlessly complicated because the parsing method
        // used to be fixed. These two can now be matched to each other.
        [this](QAbstractSocket::NetworkLayerProtocol protocol) -> CommandLine {
            Q_UNUSED(protocol)
            return {filePath("netstat"), {"-na"}};
        },

        &Port::parseFromNetstatOutput
    };
}

DeviceProcessList *QnxDevice::createProcessListModel(QObject *parent) const
{
    return new QnxDeviceProcessList(sharedFromThis(), parent);
}

DeviceTester *QnxDevice::createDeviceTester() const
{
    return new QnxDeviceTester;
}

Utils::ProcessInterface *QnxDevice::createProcessInterface() const
{
    return new QnxProcessImpl(this);
}

DeviceProcessSignalOperation::Ptr QnxDevice::signalOperation() const
{
    return DeviceProcessSignalOperation::Ptr(new QnxDeviceProcessSignalOperation(sharedFromThis()));
}

// Factory

QnxDeviceFactory::QnxDeviceFactory() : IDeviceFactory(Constants::QNX_QNX_OS_TYPE)
{
    setDisplayName(Tr::tr("QNX Device"));
    setCombinedIcon(":/qnx/images/qnxdevicesmall.png",
                    ":/qnx/images/qnxdevice.png");
    setConstructionFunction(&QnxDevice::create);
    setCreator([] {
        QnxDeviceWizard wizard;
        if (wizard.exec() != QDialog::Accepted)
            return IDevice::Ptr();
        return wizard.device();
    });
}

} // Qnx::Internal
