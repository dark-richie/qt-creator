// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconstants.h"
#include "androidmanifesteditor.h"
#include "androidmanifesteditorfactory.h"
#include "androidmanifesteditorwidget.h"
#include "androidtr.h"

#include <coreplugin/editormanager/ieditorfactory.h>

#include <texteditor/texteditoractionhandler.h>
#include <texteditor/texteditorsettings.h>

namespace Android::Internal {

class AndroidManifestEditorFactory final : public Core::IEditorFactory
{
public:
    AndroidManifestEditorFactory()
        : m_actionHandler(Constants::ANDROID_MANIFEST_EDITOR_ID,
                          Constants::ANDROID_MANIFEST_EDITOR_CONTEXT,
                          TextEditor::TextEditorActionHandler::UnCommentSelection,
                          [](Core::IEditor *editor) { return static_cast<AndroidManifestEditor *>(editor)->textEditor(); })
    {
        setId(Constants::ANDROID_MANIFEST_EDITOR_ID);
        setDisplayName(Tr::tr("Android Manifest editor"));
        addMimeType(Constants::ANDROID_MANIFEST_MIME_TYPE);
        setEditorCreator([] {
            auto androidManifestEditorWidget = new AndroidManifestEditorWidget;
            return androidManifestEditorWidget->editor();
        });
    }

private:
    TextEditor::TextEditorActionHandler m_actionHandler;
};

void setupAndroidManifestEditor()
{
    static AndroidManifestEditorFactory theAndroidManifestEditorFactory;
}

} // Android::Internal

