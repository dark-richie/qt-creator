// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "effectnode.h"
#include "compositionnode.h"
#include "uniform.h"

#include <QDir>
#include <QFileInfo>

namespace EffectMaker {

EffectNode::EffectNode(const QString &qenPath)
    : m_qenPath(qenPath)
{
    const QFileInfo fileInfo = QFileInfo(qenPath);
    m_name = fileInfo.baseName();

    QString iconPath = QStringLiteral("%1/icon/%2.svg").arg(fileInfo.absolutePath(), m_name);
    if (!QFileInfo::exists(iconPath)) {
        QDir parentDir = QDir(fileInfo.absolutePath());
        parentDir.cdUp();

        iconPath = QStringLiteral("%1/%2").arg(parentDir.path(), "placeholder.svg");
    }
    m_iconPath = QUrl::fromLocalFile(iconPath);

    CompositionNode node({}, qenPath);
    const QList<Uniform *> uniforms = node.uniforms();

    for (const Uniform *uniform : uniforms)
        m_uniformNames.insert(uniform->name());
}

QString EffectNode::name() const
{
    return m_name;
}

QString EffectNode::description() const
{
    return m_description;
}

QString EffectNode::qenPath() const
{
    return m_qenPath;
}

void EffectNode::setCanBeAdded(bool enabled)
{
    if (enabled != m_canBeAdded) {
        m_canBeAdded = enabled;
        emit canBeAddedChanged();
    }
}

bool EffectNode::hasUniform(const QString &name)
{
    return m_uniformNames.contains(name);
}

} // namespace EffectMaker

