// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectstorage.h"

#include <tracing/qmldesignertracing.h>

#include <sqlitedatabase.h>

namespace QmlDesigner {

namespace {

thread_local NanotraceHR::StringViewCategory<projectStorageTracingStatus()> projectStorageCategory_{
    "project storage"_t, Tracing::eventQueue(), projectStorageCategory};
}

NanotraceHR::StringViewCategory<projectStorageTracingStatus()> &projectStorageCategory()
{
    return projectStorageCategory_;
}

} // namespace QmlDesigner

template class QmlDesigner::ProjectStorage<Sqlite::Database>;
