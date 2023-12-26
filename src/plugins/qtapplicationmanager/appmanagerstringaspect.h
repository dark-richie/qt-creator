// Copyright (C) 2019 Luxoft Sweden AB
// Copyright (C) 2018 Pelagicore AG
// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace AppManager::Internal {

class AppManagerStringAspect : public Utils::StringAspect
{
public:
    AppManagerStringAspect(Utils::AspectContainer *container);

    QString valueOrDefault(const QString &defaultValue) const;
};

class AppManagerFilePathAspect : public Utils::FilePathAspect
{
public:
    AppManagerFilePathAspect(Utils::AspectContainer *container);

    void setButtonsVisible(bool visible);
    void setPlaceHolderPath(const QString &value);
    void setPromptDialogFilter(const QString &value);

    void addToLayout(Layouting::LayoutItem &parent) override;

    Utils::FilePath valueOrDefault(const Utils::FilePath &defaultValue) const;
    Utils::FilePath valueOrDefault(const QString &defaultValue) const;

private:
    bool validatePathWithPlaceHolder(Utils::FancyLineEdit *lineEdit, QString *errorMessage) const;
    void updateWidgets();

    QString m_placeHolderPath;
    QString m_promptDialogFilter;
    bool m_buttonsVisibile = true;
};

} // AppManager::Internal
