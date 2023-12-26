// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <tasking/barrier.h>
#include <tasking/concurrentcall.h>

#include <QtTest>
#include <QHash>

using namespace Tasking;

using namespace std::chrono;
using namespace std::chrono_literals;

using TaskObject = milliseconds;
using TestTask = TimeoutTask;

namespace PrintableEnums {

Q_NAMESPACE

// TODO: Is it possible to check for synchronous invocation of subsequent events, so that
//       we may be sure that the control didn't went back to the main event loop between 2 events?
//       In theory: yes! We can add a signal / handler to the task tree sent before / after
//       receiving done signal from each task!
// TODO: Check if the TaskTree's main guard isn't locked when receiving done signal from each task.

enum class Handler {
    Setup,
    Success,
    Error,
    Canceled,
    GroupSetup,
    GroupSuccess,
    GroupError,
    GroupCanceled,
    TweakSetupToSuccess,
    TweakSetupToError,
    TweakSetupToContinue,
    TweakDoneToSuccess,
    TweakDoneToError,
    Sync,
    BarrierAdvance,
    Timeout,
    Storage
};
Q_ENUM_NS(Handler);

enum class ThreadResult
{
    Success,
    FailOnTaskCountCheck,
    FailOnRunningCheck,
    FailOnProgressCheck,
    FailOnLogCheck,
    FailOnDoneStatusCheck,
    Canceled
};
Q_ENUM_NS(ThreadResult);

} // namespace PrintableEnums

using namespace PrintableEnums;

using Log = QList<QPair<int, Handler>>;

struct CustomStorage
{
    CustomStorage() { s_count.fetch_add(1); }
    ~CustomStorage() { s_count.fetch_add(-1); }
    Log m_log;
    static int instanceCount() { return s_count.load(); }
private:
    static std::atomic_int s_count;
};

std::atomic_int CustomStorage::s_count = 0;

struct TestData
{
    Storage<CustomStorage> storage;
    Group root;
    Log expectedLog;
    int taskCount = 0;
    DoneWith onDone = DoneWith::Success;
};

class tst_Tasking : public QObject
{
    Q_OBJECT

private slots:
    void validConstructs(); // compile test
    void runtimeCheck(); // checks done on runtime
    void testTree_data();
    void testTree();
    void testInThread_data();
    void testInThread();
    void storageIO_data();
    void storageIO();
    void storageOperators();
    void storageDestructor();
};

void tst_Tasking::validConstructs()
{
    const Group task {
        parallel,
        TestTask([](TaskObject &) {}, [](const TaskObject &, DoneWith) {}),
        TestTask([](TaskObject &) {}, [](const TaskObject &) {}),
        TestTask([](TaskObject &) {}, [](DoneWith) {}),
        TestTask([](TaskObject &) {}, [] {}),
        TestTask([](TaskObject &) {}, {}),
        TestTask([](TaskObject &) {}),
        TestTask({}, [](const TaskObject &, DoneWith) {}),
        TestTask({}, [](const TaskObject &) {}),
        TestTask({}, [](DoneWith) {}),
        TestTask({}, [] {}),
        TestTask({}, {}),
        TestTask({})
    };

    const Group group1 {
        task
    };

    const Group group2 {
        parallel,
        Group {
            parallel,
            TestTask([](TaskObject &) {}, [](const TaskObject &, DoneWith) {}),
            Group {
                parallel,
                TestTask([](TaskObject &) {}, [](const TaskObject &) {}),
                Group {
                    parallel,
                    TestTask([](TaskObject &) {}, [] {})
                }
            },
            Group {
                parallel,
                TestTask([](TaskObject &) {}, [](const TaskObject &, DoneWith) {}),
                onGroupDone([] {})
            }
        },
        task,
        onGroupDone([] {})
    };

    const auto setupHandler = [](TaskObject &) {};
    const auto finishHandler = [](const TaskObject &) {};
    const auto errorHandler = [](const TaskObject &) {};
    const auto doneHandler = [](const TaskObject &, DoneWith) {};

    const Group task2 {
        parallel,
        TestTask(),
        TestTask(setupHandler),
        TestTask(setupHandler, finishHandler, CallDoneIf::Success),
        TestTask(setupHandler, doneHandler),
        // need to explicitly pass empty handler for done
        TestTask(setupHandler, errorHandler, CallDoneIf::Error),
        TestTask({}, finishHandler, CallDoneIf::Success),
        TestTask({}, errorHandler, CallDoneIf::Error)
    };

    // When turning each of below blocks on, you should see the specific compiler error message.

    // Sync handler needs to take no arguments and has to return void or bool.
#if 0
    Sync([] { return 7; });
#endif
#if 0
    Sync([](int) { });
#endif
#if 0
    Sync([](int) { return true; });
#endif

    // Group setup handler needs to take no arguments and has to return void or SetupResult.
#if 0
    onGroupSetup([] { return 7; });
#endif
#if 0
    onGroupSetup([](int) { });
#endif

    // Group done handler needs to take (DoneWith) or (void) as an argument and has to
    // return void or bool.
#if 0
    onGroupDone([] { return 7; });
#endif
#if 0
    onGroupDone([](DoneWith) { return 7; });
#endif
#if 0
    onGroupDone([](int) { });
#endif
#if 0
    onGroupDone([](DoneWith, int) { });
#endif

    // Task setup handler needs to take (Task &) as an argument and has to return void or
    // SetupResult.
#if 0
    TestTask([] {});
#endif
#if 0
    TestTask([] { return 7; });
#endif
#if 0
    TestTask([](TaskObject &) { return 7; });
#endif
#if 0
    TestTask([](TaskObject &, int) { return SetupResult::Continue; });
#endif

    // Task done handler needs to take (const Task &, DoneWith), (const Task &),
    // (DoneWith) or (void) as arguments and has to return void or bool.
#if 0
    TestTask({}, [](const TaskObject &, DoneWith) { return 7; });
#endif
#if 0
    TestTask({}, [](const TaskObject &) { return 7; });
#endif
#if 0
    TestTask({}, [] { return 7; });
#endif
#if 0
    TestTask({}, [](const TaskObject &, DoneWith, int) {});
#endif
#if 0
    TestTask({}, [](const TaskObject &, int) {});
#endif
#if 0
    TestTask({}, [](DoneWith, int) {});
#endif
#if 0
    TestTask({}, [](int) {});
#endif
#if 0
    TestTask({}, [](const TaskObject &, DoneWith, int) { return true; });
#endif
#if 0
    TestTask({}, [](const TaskObject &, int) { return true; });
#endif
#if 0
    TestTask({}, [](DoneWith, int) { return true; });
#endif
#if 0
    TestTask({}, [](int) { return true; });
#endif
}

void tst_Tasking::runtimeCheck()
{
    {
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("^SOFT ASSERT: "));
        QTest::ignoreMessage(QtWarningMsg,
                             "Can't add the same storage into one Group twice, skipping...");
        const Storage<int> storage;

        Group {
            storage,
            storage,
        };
    }

    {
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("^SOFT ASSERT: "));
        QTest::ignoreMessage(QtWarningMsg,
                             "Can't add the same storage into one Group twice, skipping...");
        const Storage<int> storage1;
        const auto storage2 = storage1;

        Group {
            storage1,
            storage2,
        };
    }

    {
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("^SOFT ASSERT: "));
        QTest::ignoreMessage(QtWarningMsg,
                             "Group setup handler redefinition, overriding...");
        Group {
            onGroupSetup([] {}),
            onGroupSetup([] {}),
        };
    }

    {
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("^SOFT ASSERT: "));
        QTest::ignoreMessage(QtWarningMsg,
                             "Group done handler redefinition, overriding...");
        Group {
            onGroupDone([] {}),
            onGroupDone([] {}),
        };
    }

    {
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("^SOFT ASSERT: "));
        QTest::ignoreMessage(QtWarningMsg,
                             "Group execution mode redefinition, overriding...");
        Group {
            sequential,
            sequential,
        };
    }

    {
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("^SOFT ASSERT: "));
        QTest::ignoreMessage(QtWarningMsg,
                             "Group workflow policy redefinition, overriding...");
        Group {
            stopOnError,
            stopOnError,
        };
    }
}

class TickAndDone : public QObject
{
    Q_OBJECT

public:
    void setInterval(const milliseconds &interval) { m_interval = interval; }
    void start() {
        QTimer::singleShot(0, this, [this] {
            emit tick();
            QTimer::singleShot(m_interval, this, &TickAndDone::done);
        });
    }

signals:
    void tick();
    void done();

private:
    milliseconds m_interval;
};

class TickAndDoneTaskAdapter : public TaskAdapter<TickAndDone>
{
public:
    TickAndDoneTaskAdapter() { connect(task(), &TickAndDone::done, this,
                                       [this] { emit done(DoneResult::Success); }); }
    void start() final { task()->start(); }
};

using TickAndDoneTask = CustomTask<TickAndDoneTaskAdapter>;

template <typename SharedBarrierType>
GroupItem createBarrierAdvance(const Storage<CustomStorage> &storage,
                               const SharedBarrierType &barrier, int taskId)
{
    return TickAndDoneTask([storage, barrier, taskId](TickAndDone &tickAndDone) {
        tickAndDone.setInterval(1ms);
        storage->m_log.append({taskId, Handler::Setup});

        CustomStorage *currentStorage = storage.activeStorage();
        Barrier *sharedBarrier = barrier->barrier();
        QObject::connect(&tickAndDone, &TickAndDone::tick, sharedBarrier,
                         [currentStorage, sharedBarrier, taskId] {
            currentStorage->m_log.append({taskId, Handler::BarrierAdvance});
            sharedBarrier->advance();
        });
    });
}

static Handler resultToGroupHandler(DoneWith doneWith)
{
    switch (doneWith) {
    case DoneWith::Success: return Handler::GroupSuccess;
    case DoneWith::Error: return Handler::GroupError;
    case DoneWith::Cancel: return Handler::GroupCanceled;
    }
    return Handler::GroupCanceled;
}

static Handler toTweakSetupHandler(SetupResult result)
{
    switch (result) {
    case SetupResult::Continue: return Handler::TweakSetupToContinue;
    case SetupResult::StopWithSuccess: return Handler::TweakSetupToSuccess;
    case SetupResult::StopWithError: return Handler::TweakSetupToError;
    }
    return Handler::TweakSetupToContinue;
}

static Handler toTweakDoneHandler(DoneResult result)
{
    return result == DoneResult::Success ? Handler::TweakDoneToSuccess : Handler::TweakDoneToError;
}

static TestData storageShadowingData()
{
    // This test check if storage shadowing works OK.

    const Storage<CustomStorage> storage;
    // This helper storage collect the pointers to storages created by shadowedStorage.
    const Storage<QHash<int, int *>> helperStorage; // One instance in this test.
    // This storage is repeated in nested groups, the innermost storage will shadow outer ones.
    const Storage<int> shadowedStorage; // Three instances in this test.

    const auto groupSetupWithStorage = [storage, helperStorage, shadowedStorage](int taskId) {
        return onGroupSetup([storage, helperStorage, shadowedStorage, taskId] {
            storage->m_log.append({taskId, Handler::GroupSetup});
            helperStorage->insert(taskId, shadowedStorage.activeStorage());
            *shadowedStorage = taskId;
        });
    };
    const auto groupDoneWithStorage = [storage, helperStorage, shadowedStorage](int taskId) {
        return onGroupDone([storage, helperStorage, shadowedStorage, taskId](DoneWith result) {
            storage->m_log.append({taskId, resultToGroupHandler(result)});
            auto it = helperStorage->find(taskId);
            if (it == helperStorage->end()) {
                qWarning() << "The helperStorage is missing the shadowedStorage.";
                return;
            } else if (*it != shadowedStorage.activeStorage()) {
                qWarning() << "Wrong active instance of the shadowedStorage.";
                return;
            } else if (**it != taskId) {
                qWarning() << "Wrong data of the active instance of the shadowedStorage.";
                return;
            }
            helperStorage->erase(it);
            storage->m_log.append({*shadowedStorage, Handler::Storage});
        });
    };

    const Group root {
        storage,
        helperStorage,
        shadowedStorage,
        groupSetupWithStorage(1),
        Group {
            shadowedStorage,
            groupSetupWithStorage(2),
            Group {
                shadowedStorage,
                groupSetupWithStorage(3),
                groupDoneWithStorage(3)
            },
            Group {
                shadowedStorage,
                groupSetupWithStorage(4),
                groupDoneWithStorage(4)
            },
            groupDoneWithStorage(2)
        },
        groupDoneWithStorage(1)
    };

    const Log log {
        {1, Handler::GroupSetup},
        {2, Handler::GroupSetup},
        {3, Handler::GroupSetup},
        {3, Handler::GroupSuccess},
        {3, Handler::Storage},
        {4, Handler::GroupSetup},
        {4, Handler::GroupSuccess},
        {4, Handler::Storage},
        {2, Handler::GroupSuccess},
        {2, Handler::Storage},
        {1, Handler::GroupSuccess},
        {1, Handler::Storage},
    };

    return {storage, root, log, 0, DoneWith::Success};
}

static TestData parallelData()
{
    Storage<CustomStorage> storage;

    const auto setupTask = [storage](int taskId, milliseconds timeout) {
        return [storage, taskId, timeout](TaskObject &taskObject) {
            taskObject = timeout;
            storage->m_log.append({taskId, Handler::Setup});
        };
    };

    const auto setupDone = [storage](int taskId, DoneResult result = DoneResult::Success) {
        return [storage, taskId, result](DoneWith doneWith) {
            const Handler handler = doneWith == DoneWith::Cancel ? Handler::Canceled
                                    : result == DoneResult::Success ? Handler::Success : Handler::Error;
            storage->m_log.append({taskId, handler});
            return doneWith == DoneWith::Cancel ? DoneResult::Error
                   : result == DoneResult::Success ? DoneResult::Success : DoneResult::Error;
        };
    };

    const auto createTask = [storage, setupTask, setupDone](
                                int taskId, DoneResult result, milliseconds timeout = 0ms) {
        return TestTask(setupTask(taskId, timeout), setupDone(taskId, result));
    };

    const auto createSuccessTask = [createTask](int taskId, milliseconds timeout = 0ms) {
        return createTask(taskId, DoneResult::Success, timeout);
    };

    const auto groupDone = [storage](int taskId) {
        return onGroupDone([storage, taskId](DoneWith result) {
            storage->m_log.append({taskId, resultToGroupHandler(result)});
        });
    };

    const Group root {
        storage,
        parallel,
        createSuccessTask(1),
        createSuccessTask(2),
        createSuccessTask(3),
        createSuccessTask(4),
        createSuccessTask(5),
        groupDone(0)
    };

    const Log log {
        {1, Handler::Setup}, // Setup order is determined in parallel mode
        {2, Handler::Setup},
        {3, Handler::Setup},
        {4, Handler::Setup},
        {5, Handler::Setup},
        {1, Handler::Success},
        {2, Handler::Success},
        {3, Handler::Success},
        {4, Handler::Success},
        {5, Handler::Success},
        {0, Handler::GroupSuccess}
    };

    return {storage, root, log, 5, DoneWith::Success};
}

void tst_Tasking::testTree_data()
{
    QTest::addColumn<TestData>("testData");

    Storage<CustomStorage> storage;

    const auto setupTask = [storage](int taskId, milliseconds timeout) {
        return [storage, taskId, timeout](TaskObject &taskObject) {
            taskObject = timeout;
            storage->m_log.append({taskId, Handler::Setup});
        };
    };

    const auto setupTaskWithTweak = [storage](int taskId, SetupResult result) {
        return [storage, taskId, result](TaskObject &) {
            storage->m_log.append({taskId, Handler::Setup});
            storage->m_log.append({taskId, toTweakSetupHandler(result)});
            return result;
        };
    };

    const auto setupDone = [storage](int taskId, DoneResult result = DoneResult::Success) {
        return [storage, taskId, result](DoneWith doneWith) {
            const Handler handler = doneWith == DoneWith::Cancel ? Handler::Canceled
                                    : result == DoneResult::Success ? Handler::Success : Handler::Error;
            storage->m_log.append({taskId, handler});
            return doneWith == DoneWith::Cancel ? DoneResult::Error
                   : result == DoneResult::Success ? DoneResult::Success : DoneResult::Error;
        };
    };

    const auto setupTimeout = [storage](int taskId) {
        return [storage, taskId] {
            storage->m_log.append({taskId, Handler::Timeout});
        };
    };

    const auto createTask = [storage, setupTask, setupDone](
            int taskId, DoneResult result, milliseconds timeout = 0ms) {
        return TestTask(setupTask(taskId, timeout), setupDone(taskId, result));
    };

    const auto createSuccessTask = [createTask](int taskId, milliseconds timeout = 0ms) {
        return createTask(taskId, DoneResult::Success, timeout);
    };

    const auto createFailingTask = [createTask](int taskId, milliseconds timeout = 0ms) {
        return createTask(taskId, DoneResult::Error, timeout);
    };

    const auto createTaskWithSetupTweak = [storage, setupTaskWithTweak, setupDone](
                                          int taskId, SetupResult desiredResult) {
        return TestTask(setupTaskWithTweak(taskId, desiredResult), setupDone(taskId));
    };

    const auto groupSetup = [storage](int taskId) {
        return onGroupSetup([storage, taskId] {
            storage->m_log.append({taskId, Handler::GroupSetup});
        });
    };
    const auto groupDone = [storage](int taskId) {
        return onGroupDone([storage, taskId](DoneWith result) {
            storage->m_log.append({taskId, resultToGroupHandler(result)});
        });
    };
    const auto groupSetupWithTweak = [storage](int taskId, SetupResult desiredResult) {
        return onGroupSetup([storage, taskId, desiredResult] {
            storage->m_log.append({taskId, Handler::GroupSetup});
            storage->m_log.append({taskId, toTweakSetupHandler(desiredResult)});
            return desiredResult;
        });
    };
    const auto groupDoneWithTweak = [storage](int taskId, DoneResult result) {
        return onGroupDone([storage, taskId, result](DoneWith doneWith) {
            storage->m_log.append({taskId, resultToGroupHandler(doneWith)});
            storage->m_log.append({taskId, toTweakDoneHandler(result)});
            return result;
        });
    };
    const auto createSync = [storage](int taskId) {
        return Sync([storage, taskId] { storage->m_log.append({taskId, Handler::Sync}); });
    };
    const auto createSyncWithTweak = [storage](int taskId, DoneResult result) {
        return Sync([storage, taskId, result] {
            storage->m_log.append({taskId, Handler::Sync});
            storage->m_log.append({taskId, toTweakDoneHandler(result)});
            return result;
        });
    };

    {
        const Group root1 {
            storage,
            groupDone(0)
        };
        const Group root2 {
            storage,
            onGroupSetup([] { return SetupResult::Continue; }),
            groupDone(0)
        };
        const Group root3 {
            storage,
            onGroupSetup([] { return SetupResult::StopWithSuccess; }),
            groupDone(0)
        };
        const Group root4 {
            storage,
            onGroupSetup([] { return SetupResult::StopWithError; }),
            groupDone(0)
        };

        const Log logDone {{0, Handler::GroupSuccess}};
        const Log logError {{0, Handler::GroupError}};

        QTest::newRow("Empty") << TestData{storage, root1, logDone, 0, DoneWith::Success};
        QTest::newRow("EmptyContinue") << TestData{storage, root2, logDone, 0, DoneWith::Success};
        QTest::newRow("EmptyDone") << TestData{storage, root3, logDone, 0, DoneWith::Success};
        QTest::newRow("EmptyError") << TestData{storage, root4, logError, 0, DoneWith::Error};
    }

    {
        const auto setupGroup = [=](SetupResult setupResult, WorkflowPolicy policy) {
            return Group {
                storage,
                workflowPolicy(policy),
                onGroupSetup([setupResult] { return setupResult; }),
                groupDone(0)
            };
        };

        const auto doneData = [storage, setupGroup](WorkflowPolicy policy) {
            return TestData{storage, setupGroup(SetupResult::StopWithSuccess, policy),
                            Log{{0, Handler::GroupSuccess}}, 0, DoneWith::Success};
        };
        const auto errorData = [storage, setupGroup](WorkflowPolicy policy) {
            return TestData{storage, setupGroup(SetupResult::StopWithError, policy),
                            Log{{0, Handler::GroupError}}, 0, DoneWith::Error};
        };

        QTest::newRow("DoneAndStopOnError") << doneData(WorkflowPolicy::StopOnError);
        QTest::newRow("DoneAndContinueOnError") << doneData(WorkflowPolicy::ContinueOnError);
        QTest::newRow("DoneAndStopOnSuccess") << doneData(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("DoneAndContinueOnSuccess") << doneData(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("DoneAndStopOnSuccessOrError") << doneData(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("DoneAndFinishAllAndSuccess") << doneData(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("DoneAndFinishAllAndError") << doneData(WorkflowPolicy::FinishAllAndError);

        QTest::newRow("ErrorAndStopOnError") << errorData(WorkflowPolicy::StopOnError);
        QTest::newRow("ErrorAndContinueOnError") << errorData(WorkflowPolicy::ContinueOnError);
        QTest::newRow("ErrorAndStopOnSuccess") << errorData(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("ErrorAndContinueOnSuccess") << errorData(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("ErrorAndStopOnSuccessOrError") << errorData(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("ErrorAndFinishAllAndSuccess") << errorData(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("ErrorAndFinishAllAndError") << errorData(WorkflowPolicy::FinishAllAndError);
    }

    {
        const Group root {
            storage,
            createTaskWithSetupTweak(1, SetupResult::StopWithSuccess),
            createTaskWithSetupTweak(2, SetupResult::StopWithSuccess)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToSuccess},
            {2, Handler::Setup},
            {2, Handler::TweakSetupToSuccess}
        };
        QTest::newRow("TweekTaskSuccess") << TestData{storage, root, log, 2, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            createTaskWithSetupTweak(1, SetupResult::StopWithError),
            createTaskWithSetupTweak(2, SetupResult::StopWithError)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToError}
        };
        QTest::newRow("TweekTaskError") << TestData{storage, root, log, 2, DoneWith::Error};
    }

    {
        const Group root {
            storage,
            createTaskWithSetupTweak(1, SetupResult::Continue),
            createTaskWithSetupTweak(2, SetupResult::Continue),
            createTaskWithSetupTweak(3, SetupResult::StopWithError),
            createTaskWithSetupTweak(4, SetupResult::Continue)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToContinue},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::TweakSetupToContinue},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError}
        };
        QTest::newRow("TweekMixed") << TestData{storage, root, log, 4, DoneWith::Error};
    }

    {
        const Group root {
            parallel,
            storage,
            createTaskWithSetupTweak(1, SetupResult::Continue),
            createTaskWithSetupTweak(2, SetupResult::Continue),
            createTaskWithSetupTweak(3, SetupResult::StopWithError),
            createTaskWithSetupTweak(4, SetupResult::Continue)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToContinue},
            {2, Handler::Setup},
            {2, Handler::TweakSetupToContinue},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError},
            {1, Handler::Canceled},
            {2, Handler::Canceled}
        };
        QTest::newRow("TweekParallel") << TestData{storage, root, log, 4, DoneWith::Error};
    }

    {
        const Group root {
            parallel,
            storage,
            createTaskWithSetupTweak(1, SetupResult::Continue),
            createTaskWithSetupTweak(2, SetupResult::Continue),
            Group {
                createTaskWithSetupTweak(3, SetupResult::StopWithError)
            },
            createTaskWithSetupTweak(4, SetupResult::Continue)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToContinue},
            {2, Handler::Setup},
            {2, Handler::TweakSetupToContinue},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError},
            {1, Handler::Canceled},
            {2, Handler::Canceled}
        };
        QTest::newRow("TweekParallelGroup") << TestData{storage, root, log, 4, DoneWith::Error};
    }

    {
        const Group root {
            parallel,
            storage,
            createTaskWithSetupTweak(1, SetupResult::Continue),
            createTaskWithSetupTweak(2, SetupResult::Continue),
            Group {
                groupSetupWithTweak(0, SetupResult::StopWithError),
                createTaskWithSetupTweak(3, SetupResult::Continue)
            },
            createTaskWithSetupTweak(4, SetupResult::Continue)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToContinue},
            {2, Handler::Setup},
            {2, Handler::TweakSetupToContinue},
            {0, Handler::GroupSetup},
            {0, Handler::TweakSetupToError},
            {1, Handler::Canceled},
            {2, Handler::Canceled}
        };
        QTest::newRow("TweekParallelGroupSetup")
            << TestData{storage, root, log, 4, DoneWith::Error};
    }

    {
        const Group root {
            storage,
            Group {
                Group {
                    Group {
                        Group {
                            Group {
                                createSuccessTask(5),
                                groupSetup(5),
                                groupDone(5)
                            },
                            groupSetup(4),
                            groupDone(4)
                        },
                        groupSetup(3),
                        groupDone(3)
                    },
                    groupSetup(2),
                    groupDone(2)
                },
                groupSetup(1),
                groupDone(1)
            },
            groupDone(0)
        };
        const Log log {
            {1, Handler::GroupSetup},
            {2, Handler::GroupSetup},
            {3, Handler::GroupSetup},
            {4, Handler::GroupSetup},
            {5, Handler::GroupSetup},
            {5, Handler::Setup},
            {5, Handler::Success},
            {5, Handler::GroupSuccess},
            {4, Handler::GroupSuccess},
            {3, Handler::GroupSuccess},
            {2, Handler::GroupSuccess},
            {1, Handler::GroupSuccess},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("Nested") << TestData{storage, root, log, 1, DoneWith::Success};
    }

    QTest::newRow("Parallel") << parallelData();

    {
        auto setupSubTree = [storage, createSuccessTask](TaskTree &taskTree) {
            const Group nestedRoot {
                storage,
                createSuccessTask(2),
                createSuccessTask(3),
                createSuccessTask(4)
            };
            taskTree.setRecipe(nestedRoot);
            CustomStorage *activeStorage = storage.activeStorage();
            const auto collectSubLog = [activeStorage](const CustomStorage &subTreeStorage){
                activeStorage->m_log += subTreeStorage.m_log;
            };
            taskTree.onStorageDone(storage, collectSubLog);
        };
        const Group root1 {
            storage,
            createSuccessTask(1),
            createSuccessTask(2),
            createSuccessTask(3),
            createSuccessTask(4),
            createSuccessTask(5),
            groupDone(0)
        };
        const Group root2 {
            storage,
            Group { createSuccessTask(1) },
            Group { createSuccessTask(2) },
            Group { createSuccessTask(3) },
            Group { createSuccessTask(4) },
            Group { createSuccessTask(5) },
            groupDone(0)
        };
        const Group root3 {
            storage,
            createSuccessTask(1),
            TaskTreeTask(setupSubTree),
            createSuccessTask(5),
            groupDone(0)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Setup},
            {4, Handler::Success},
            {5, Handler::Setup},
            {5, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("Sequential") << TestData{storage, root1, log, 5, DoneWith::Success};
        QTest::newRow("SequentialEncapsulated") << TestData{storage, root2, log, 5, DoneWith::Success};
        // We don't inspect subtrees, so taskCount is 3, not 5.
        QTest::newRow("SequentialSubTree") << TestData{storage, root3, log, 3, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            Group {
                createSuccessTask(1),
                Group {
                    createSuccessTask(2),
                    Group {
                        createSuccessTask(3),
                        Group {
                            createSuccessTask(4),
                            Group {
                                createSuccessTask(5),
                                groupDone(5)
                            },
                            groupDone(4)
                        },
                        groupDone(3)
                    },
                    groupDone(2)
                },
                groupDone(1)
            },
            groupDone(0)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Setup},
            {4, Handler::Success},
            {5, Handler::Setup},
            {5, Handler::Success},
            {5, Handler::GroupSuccess},
            {4, Handler::GroupSuccess},
            {3, Handler::GroupSuccess},
            {2, Handler::GroupSuccess},
            {1, Handler::GroupSuccess},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("SequentialNested") << TestData{storage, root, log, 5, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            createSuccessTask(1),
            createSuccessTask(2),
            createFailingTask(3),
            createSuccessTask(4),
            createSuccessTask(5),
            groupDone(0)
        };
        const Log log {
            {1, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Error},
            {0, Handler::GroupError}
        };
        QTest::newRow("SequentialError") << TestData{storage, root, log, 5, DoneWith::Error};
    }

    {
        const auto createRoot = [storage, groupDone](WorkflowPolicy policy) {
            return Group {
                storage,
                workflowPolicy(policy),
                groupDone(0)
            };
        };

        const Log doneLog = {{0, Handler::GroupSuccess}};
        const Log errorLog = {{0, Handler::GroupError}};

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("EmptyStopOnError") << TestData{storage, root1, doneLog, 0,
                                                      DoneWith::Success};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("EmptyContinueOnError") << TestData{storage, root2, doneLog, 0,
                                                          DoneWith::Success};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("EmptyStopOnSuccess") << TestData{storage, root3, errorLog, 0,
                                                     DoneWith::Error};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("EmptyContinueOnSuccess") << TestData{storage, root4, errorLog, 0,
                                                         DoneWith::Error};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("EmptyStopOnSuccessOrError") << TestData{storage, root5, errorLog, 0,
                                                         DoneWith::Error};

        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("EmptyFinishAllAndSuccess") << TestData{storage, root6, doneLog, 0,
                                                           DoneWith::Success};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("EmptyFinishAllAndError") << TestData{storage, root7, errorLog, 0,
                                                            DoneWith::Error};
    }

    {
        const auto createRoot = [storage, createSuccessTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                workflowPolicy(policy),
                createSuccessTask(1),
                groupDone(0)
            };
        };

        const Log doneLog = {
            {1, Handler::Setup},
            {1, Handler::Success},
            {0, Handler::GroupSuccess}
        };

        const Log errorLog = {
            {1, Handler::Setup},
            {1, Handler::Success},
            {0, Handler::GroupError}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("DoneStopOnError") << TestData{storage, root1, doneLog, 1,
                                                     DoneWith::Success};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("DoneContinueOnError") << TestData{storage, root2, doneLog, 1,
                                                         DoneWith::Success};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("DoneStopOnSuccess") << TestData{storage, root3, doneLog, 1,
                                                    DoneWith::Success};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("DoneContinueOnSuccess") << TestData{storage, root4, doneLog, 1,
                                                        DoneWith::Success};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("DoneStopOnSuccessOrError") << TestData{storage, root5, doneLog, 1,
                                                        DoneWith::Success};

        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("DoneFinishAllAndSuccess") << TestData{storage, root6, doneLog, 1,
                                                          DoneWith::Success};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("DoneFinishAllAndError") << TestData{storage, root7, errorLog, 1,
                                                           DoneWith::Error};
    }

    {
        const auto createRoot = [storage, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                workflowPolicy(policy),
                createFailingTask(1),
                groupDone(0)
            };
        };

        const Log doneLog = {
            {1, Handler::Setup},
            {1, Handler::Error},
            {0, Handler::GroupSuccess}
        };

        const Log errorLog = {
            {1, Handler::Setup},
            {1, Handler::Error},
            {0, Handler::GroupError}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("ErrorStopOnError") << TestData{storage, root1, errorLog, 1,
                                                      DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("ErrorContinueOnError") << TestData{storage, root2, errorLog, 1,
                                                          DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("ErrorStopOnSuccess") << TestData{storage, root3, errorLog, 1,
                                                     DoneWith::Error};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("ErrorContinueOnSuccess") << TestData{storage, root4, errorLog, 1,
                                                         DoneWith::Error};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("ErrorStopOnSuccessOrError") << TestData{storage, root5, errorLog, 1,
                                                         DoneWith::Error};

        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("ErrorFinishAllAndSuccess") << TestData{storage, root6, doneLog, 1,
                                                           DoneWith::Success};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("ErrorFinishAllAndError") << TestData{storage, root7, errorLog, 1,
                                                           DoneWith::Error};
    }

    {
        // These tests check whether the proper root's group end handler is called
        // when the group is stopped. Test it with different workflow policies.
        // The root starts one short failing task together with one long task.
        const auto createRoot = [storage, createSuccessTask, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                parallel,
                workflowPolicy(policy),
                createFailingTask(1, 1ms),
                createSuccessTask(2, 1ms),
                groupDone(0)
            };
        };

        const Log errorErrorLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {1, Handler::Error},
            {2, Handler::Canceled},
            {0, Handler::GroupError}
        };

        const Log errorDoneLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {1, Handler::Error},
            {2, Handler::Success},
            {0, Handler::GroupError}
        };

        const Log doneLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {1, Handler::Error},
            {2, Handler::Success},
            {0, Handler::GroupSuccess}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("StopRootWithStopOnError")
            << TestData{storage, root1, errorErrorLog, 2, DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("StopRootWithContinueOnError")
            << TestData{storage, root2, errorDoneLog, 2, DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("StopRootWithStopOnSuccess")
            << TestData{storage, root3, doneLog, 2, DoneWith::Success};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("StopRootWithContinueOnSuccess")
            << TestData{storage, root4, doneLog, 2, DoneWith::Success};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("StopRootWithStopOnSuccessOrError")
            << TestData{storage, root5, errorErrorLog, 2, DoneWith::Error};

        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("StopRootWithFinishAllAndSuccess")
            << TestData{storage, root6, doneLog, 2, DoneWith::Success};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("StopRootWithFinishAllAndError")
            << TestData{storage, root7, errorDoneLog, 2, DoneWith::Error};
    }

    {
        // These tests check whether the proper root's group end handler is called
        // when the group is stopped. Test it with different workflow policies.
        // The root starts in parallel: one very short successful task, one short failing task
        // and one long task.
        const auto createRoot = [storage, createSuccessTask, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                parallel,
                workflowPolicy(policy),
                createSuccessTask(1),
                createFailingTask(2, 1ms),
                createSuccessTask(3, 1ms),
                groupDone(0)
            };
        };

        const Log errorErrorLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Error},
            {3, Handler::Canceled},
            {0, Handler::GroupError}
        };

        const Log errorDoneLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Error},
            {3, Handler::Success},
            {0, Handler::GroupError}
        };

        const Log doneErrorLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Canceled},
            {3, Handler::Canceled},
            {0, Handler::GroupSuccess}
        };

        const Log doneDoneLog = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Error},
            {3, Handler::Success},
            {0, Handler::GroupSuccess}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("StopRootAfterDoneWithStopOnError")
            << TestData{storage, root1, errorErrorLog, 3, DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("StopRootAfterDoneWithContinueOnError")
            << TestData{storage, root2, errorDoneLog, 3, DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("StopRootAfterDoneWithStopOnSuccess")
            << TestData{storage, root3, doneErrorLog, 3, DoneWith::Success};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("StopRootAfterDoneWithContinueOnSuccess")
            << TestData{storage, root4, doneDoneLog, 3, DoneWith::Success};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("StopRootAfterDoneWithStopOnSuccessOrError")
            << TestData{storage, root5, doneErrorLog, 3, DoneWith::Success};

        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("StopRootAfterDoneWithFinishAllAndSuccess")
            << TestData{storage, root6, doneDoneLog, 3, DoneWith::Success};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("StopRootAfterDoneWithFinishAllAndError")
            << TestData{storage, root7, errorDoneLog, 3, DoneWith::Error};
    }

    {
        // These tests check whether the proper subgroup's end handler is called
        // when the group is stopped. Test it with different workflow policies.
        // The subgroup starts one long task.
        const auto createRoot = [storage, createSuccessTask, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                parallel,
                Group {
                    workflowPolicy(policy),
                    createSuccessTask(1, 1000ms),
                    groupDone(1)
                },
                createFailingTask(2, 1ms),
                groupDone(2)
            };
        };

        const Log log = {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {2, Handler::Error},
            {1, Handler::Canceled},
            {1, Handler::GroupCanceled},
            {2, Handler::GroupError}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("StopGroupWithStopOnError")
            << TestData{storage, root1, log, 2, DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("StopGroupWithContinueOnError")
            << TestData{storage, root2, log, 2, DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("StopGroupWithStopOnSuccess")
            << TestData{storage, root3, log, 2, DoneWith::Error};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("StopGroupWithContinueOnSuccess")
            << TestData{storage, root4, log, 2, DoneWith::Error};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("StopGroupWithStopOnSuccessOrError")
            << TestData{storage, root5, log, 2, DoneWith::Error};

        // TODO: Behavioral change! Fix Docs!
        // Cancellation always invokes error handler (i.e. DoneWith is Canceled)
        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("StopGroupWithFinishAllAndSuccess")
            << TestData{storage, root6, log, 2, DoneWith::Error};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("StopGroupWithFinishAllAndError")
            << TestData{storage, root7, log, 2, DoneWith::Error};
    }

    {
        // These tests check whether the proper subgroup's end handler is called
        // when the group is stopped. Test it with different workflow policies.
        // The sequential subgroup starts one short successful task followed by one long task.
        const auto createRoot = [storage, createSuccessTask, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                parallel,
                Group {
                    workflowPolicy(policy),
                    createSuccessTask(1),
                    createSuccessTask(2, 1000ms),
                    groupDone(1)
                },
                createFailingTask(3, 1ms),
                groupDone(2)
            };
        };

        const Log errorLog = {
            {1, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {3, Handler::Error},
            {2, Handler::Canceled},
            {1, Handler::GroupCanceled},
            {2, Handler::GroupError}
        };

        const Log doneLog = {
            {1, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Success},
            {1, Handler::GroupSuccess},
            {3, Handler::Error},
            {2, Handler::GroupError}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("StopGroupAfterDoneWithStopOnError")
            << TestData{storage, root1, errorLog, 3, DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("StopGroupAfterDoneWithContinueOnError")
            << TestData{storage, root2, errorLog, 3, DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("StopGroupAfterDoneWithStopOnSuccess")
            << TestData{storage, root3, doneLog, 3, DoneWith::Error};

        // TODO: Behavioral change!
        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("StopGroupAfterDoneWithContinueOnSuccess")
            << TestData{storage, root4, errorLog, 3, DoneWith::Error};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("StopGroupAfterDoneWithStopOnSuccessOrError")
            << TestData{storage, root5, doneLog, 3, DoneWith::Error};

        // TODO: Behavioral change!
        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("StopGroupAfterDoneWithFinishAllAndSuccess")
            << TestData{storage, root6, errorLog, 3, DoneWith::Error};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("StopGroupAfterDoneWithFinishAllAndError")
            << TestData{storage, root7, errorLog, 3, DoneWith::Error};
    }

    {
        // These tests check whether the proper subgroup's end handler is called
        // when the group is stopped. Test it with different workflow policies.
        // The sequential subgroup starts one short failing task followed by one long task.
        const auto createRoot = [storage, createSuccessTask, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                parallel,
                Group {
                    workflowPolicy(policy),
                    createFailingTask(1),
                    createSuccessTask(2, 1000ms),
                    groupDone(1)
                },
                createFailingTask(3, 1ms),
                groupDone(2)
            };
        };

        const Log shortLog = {
            {1, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Error},
            {1, Handler::GroupError},
            {3, Handler::Canceled},
            {2, Handler::GroupError}
        };

        const Log longLog = {
            {1, Handler::Setup},
            {3, Handler::Setup},
            {1, Handler::Error},
            {2, Handler::Setup},
            {3, Handler::Error},
            {2, Handler::Canceled},
            {1, Handler::GroupCanceled},
            {2, Handler::GroupError}
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        QTest::newRow("StopGroupAfterErrorWithStopOnError")
            << TestData{storage, root1, shortLog, 3, DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        QTest::newRow("StopGroupAfterErrorWithContinueOnError")
            << TestData{storage, root2, longLog, 3, DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        QTest::newRow("StopGroupAfterErrorWithStopOnSuccess")
            << TestData{storage, root3, longLog, 3, DoneWith::Error};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        QTest::newRow("StopGroupAfterErrorWithContinueOnSuccess")
            << TestData{storage, root4, longLog, 3, DoneWith::Error};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        QTest::newRow("StopGroupAfterErrorWithStopOnSuccessOrError")
            << TestData{storage, root5, shortLog, 3, DoneWith::Error};

        // TODO: Behavioral change!
        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("StopGroupAfterErrorWithFinishAllAndSuccess")
            << TestData{storage, root6, longLog, 3, DoneWith::Error};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("StopGroupAfterErrorWithFinishAllAndError")
            << TestData{storage, root7, longLog, 3, DoneWith::Error};
    }

    {
        const auto createRoot = [storage, createSuccessTask, createFailingTask, groupDone](
                                    WorkflowPolicy policy) {
            return Group {
                storage,
                workflowPolicy(policy),
                createSuccessTask(1),
                createFailingTask(2),
                createSuccessTask(3),
                groupDone(0)
            };
        };

        const Group root1 = createRoot(WorkflowPolicy::StopOnError);
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Error},
            {0, Handler::GroupError}
        };
        QTest::newRow("StopOnError") << TestData{storage, root1, log1, 3, DoneWith::Error};

        const Group root2 = createRoot(WorkflowPolicy::ContinueOnError);
        const Log errorLog {
            {1, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Error},
            {3, Handler::Setup},
            {3, Handler::Success},
            {0, Handler::GroupError}
        };
        QTest::newRow("ContinueOnError") << TestData{storage, root2, errorLog, 3, DoneWith::Error};

        const Group root3 = createRoot(WorkflowPolicy::StopOnSuccess);
        const Log log3 {
            {1, Handler::Setup},
            {1, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("StopOnSuccess") << TestData{storage, root3, log3, 3, DoneWith::Success};

        const Group root4 = createRoot(WorkflowPolicy::ContinueOnSuccess);
        const Log doneLog {
            {1, Handler::Setup},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Error},
            {3, Handler::Setup},
            {3, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("ContinueOnSuccess") << TestData{storage, root4, doneLog, 3, DoneWith::Success};

        const Group root5 = createRoot(WorkflowPolicy::StopOnSuccessOrError);
        const Log log5 {
            {1, Handler::Setup},
            {1, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("StopOnSuccessOrError") << TestData{storage, root5, log5, 3, DoneWith::Success};

        const Group root6 = createRoot(WorkflowPolicy::FinishAllAndSuccess);
        QTest::newRow("FinishAllAndSuccess") << TestData{storage, root6, doneLog, 3, DoneWith::Success};

        const Group root7 = createRoot(WorkflowPolicy::FinishAllAndError);
        QTest::newRow("FinishAllAndError") << TestData{storage, root7, errorLog, 3, DoneWith::Error};
    }

    {
        const auto createRoot = [storage, createTask, groupDone](DoneResult firstResult,
                                                                 DoneResult secondResult) {
            return Group {
                parallel,
                stopOnSuccessOrError,
                storage,
                createTask(1, firstResult, 1000ms),
                createTask(2, secondResult, 1ms),
                groupDone(0)
            };
        };

        const Group root1 = createRoot(DoneResult::Success, DoneResult::Success);
        const Group root2 = createRoot(DoneResult::Success, DoneResult::Error);
        const Group root3 = createRoot(DoneResult::Error, DoneResult::Success);
        const Group root4 = createRoot(DoneResult::Error, DoneResult::Error);

        const Log success {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {2, Handler::Success},
            {1, Handler::Canceled},
            {0, Handler::GroupSuccess}
        };
        const Log failure {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {2, Handler::Error},
            {1, Handler::Canceled},
            {0, Handler::GroupError}
        };

        QTest::newRow("StopOnSuccessOrError1")
            << TestData{storage, root1, success, 2, DoneWith::Success};
        QTest::newRow("StopOnSuccessOrError2")
            << TestData{storage, root2, failure, 2, DoneWith::Error};
        QTest::newRow("StopOnSuccessOrError3")
            << TestData{storage, root3, success, 2, DoneWith::Success};
        QTest::newRow("StopOnSuccessOrError4")
            << TestData{storage, root4, failure, 2, DoneWith::Error};
    }

    {
        // This test checks whether group setup handler's result is properly dispatched.
        const auto createRoot = [storage, createSuccessTask, groupDone, groupSetupWithTweak](
                                    SetupResult desiredResult) {
            return Group {
                storage,
                Group {
                    groupSetupWithTweak(1, desiredResult),
                    createSuccessTask(1)
                },
                groupDone(0)
            };
        };

        const Group root1 = createRoot(SetupResult::StopWithSuccess);
        const Log log1 {
            {1, Handler::GroupSetup},
            {1, Handler::TweakSetupToSuccess},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("GroupSetupTweakToSuccess")
            << TestData{storage, root1, log1, 1, DoneWith::Success};

        const Group root2 = createRoot(SetupResult::StopWithError);
        const Log log2 {
            {1, Handler::GroupSetup},
            {1, Handler::TweakSetupToError},
            {0, Handler::GroupError}
        };
        QTest::newRow("GroupSetupTweakToError")
            << TestData{storage, root2, log2, 1, DoneWith::Error};

        const Group root3 = createRoot(SetupResult::Continue);
        const Log log3 {
            {1, Handler::GroupSetup},
            {1, Handler::TweakSetupToContinue},
            {1, Handler::Setup},
            {1, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("GroupSetupTweakToContinue")
            << TestData{storage, root3, log3, 1, DoneWith::Success};
    }

    {
        // This test checks whether group done handler's result is properly dispatched.
        const auto createRoot = [storage, createTask, groupDone, groupDoneWithTweak](
                                 DoneResult firstResult, DoneResult secondResult) {
            return Group {
                storage,
                Group {
                    createTask(1, firstResult),
                    groupDoneWithTweak(1, secondResult)
                },
                groupDone(0)
            };
        };

        const Group root1 = createRoot(DoneResult::Success, DoneResult::Success);
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::Success},
            {1, Handler::GroupSuccess},
            {1, Handler::TweakDoneToSuccess},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("GroupDoneWithSuccessTweakToSuccess")
            << TestData{storage, root1, log1, 1, DoneWith::Success};

        const Group root2 = createRoot(DoneResult::Success, DoneResult::Error);
        const Log log2 {
            {1, Handler::Setup},
            {1, Handler::Success},
            {1, Handler::GroupSuccess},
            {1, Handler::TweakDoneToError},
            {0, Handler::GroupError}
        };
        QTest::newRow("GroupDoneWithSuccessTweakToError")
            << TestData{storage, root2, log2, 1, DoneWith::Error};

        const Group root3 = createRoot(DoneResult::Error, DoneResult::Success);
        const Log log3 {
            {1, Handler::Setup},
            {1, Handler::Error},
            {1, Handler::GroupError},
            {1, Handler::TweakDoneToSuccess},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("GroupDoneWithErrorTweakToSuccess")
            << TestData{storage, root3, log3, 1, DoneWith::Success};

        const Group root4 = createRoot(DoneResult::Error, DoneResult::Error);
        const Log log4 {
            {1, Handler::Setup},
            {1, Handler::Error},
            {1, Handler::GroupError},
            {1, Handler::TweakDoneToError},
            {0, Handler::GroupError}
        };
        QTest::newRow("GroupDoneWithErrorTweakToError")
            << TestData{storage, root4, log4, 1, DoneWith::Error};
    }

    {
        // This test checks whether task setup handler's result is properly dispatched.
        const auto createRoot = [storage, createSuccessTask, groupDone, createTaskWithSetupTweak](
                                    SetupResult desiredResult) {
            return Group {
                storage,
                Group {
                    createTaskWithSetupTweak(1, desiredResult),
                    createSuccessTask(2)
                },
                groupDone(0)
            };
        };

        const Group root1 = createRoot(SetupResult::StopWithSuccess);
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToSuccess},
            {2, Handler::Setup},
            {2, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("TaskSetupTweakToSuccess")
            << TestData{storage, root1, log1, 2, DoneWith::Success};

        const Group root2 = createRoot(SetupResult::StopWithError);
        const Log log2 {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToError},
            {0, Handler::GroupError}
        };
        QTest::newRow("TaskSetupTweakToError")
            << TestData{storage, root2, log2, 2, DoneWith::Error};

        const Group root3 = createRoot(SetupResult::Continue);
        const Log log3 {
            {1, Handler::Setup},
            {1, Handler::TweakSetupToContinue},
            {1, Handler::Success},
            {2, Handler::Setup},
            {2, Handler::Success},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("TaskSetupTweakToContinue")
            << TestData{storage, root3, log3, 2, DoneWith::Success};
    }

    {
        const Group root {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                createSuccessTask(1)
            },
            Group {
                groupSetup(2),
                createSuccessTask(2)
            },
            Group {
                groupSetup(3),
                createSuccessTask(3)
            },
            Group {
                groupSetup(4),
                createSuccessTask(4)
            }
        };
        const Log log {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {1, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {2, Handler::Success},
            {4, Handler::GroupSetup},
            {4, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Success}
        };
        QTest::newRow("NestedParallel") << TestData{storage, root, log, 4, DoneWith::Success};
    }

    {
        const Group root {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                createSuccessTask(1)
            },
            Group {
                groupSetup(2),
                createSuccessTask(2)
            },
            Group {
                groupSetup(3),
                createTaskWithSetupTweak(3, SetupResult::StopWithSuccess)
            },
            Group {
                groupSetup(4),
                createSuccessTask(4)
            },
            Group {
                groupSetup(5),
                createSuccessTask(5)
            }
        };
        const Log log {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {1, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToSuccess},
            {4, Handler::GroupSetup},
            {4, Handler::Setup},
            {2, Handler::Success},
            {5, Handler::GroupSetup},
            {5, Handler::Setup},
            {4, Handler::Success},
            {5, Handler::Success}
        };
        QTest::newRow("NestedParallelDone") << TestData{storage, root, log, 5, DoneWith::Success};
    }

    {
        const Group root1 {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                createSuccessTask(1)
            },
            Group {
                groupSetup(2),
                createSuccessTask(2)
            },
            Group {
                groupSetup(3),
                createTaskWithSetupTweak(3, SetupResult::StopWithError)
            },
            Group {
                groupSetup(4),
                createSuccessTask(4)
            },
            Group {
                groupSetup(5),
                createSuccessTask(5)
            }
        };
        const Log log1 {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {1, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError},
            {2, Handler::Canceled}
        };

        // Inside this test the task 2 should finish first, then synchonously:
        // - task 3 should exit setup with error
        // - task 1 should be stopped as a consequence of the error inside the group
        // - tasks 4 and 5 should be skipped
        const Group root2 {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                createSuccessTask(1, 10ms)
            },
            Group {
                groupSetup(2),
                createSuccessTask(2)
            },
            Group {
                groupSetup(3),
                createTaskWithSetupTweak(3, SetupResult::StopWithError)
            },
            Group {
                groupSetup(4),
                createSuccessTask(4)
            },
            Group {
                groupSetup(5),
                createSuccessTask(5)
            }
        };
        const Log log2 {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError},
            {1, Handler::Canceled}
        };

        // This test ensures that the task 1 doesn't invoke its done handler,
        // being ready while sleeping in the task's 2 done handler.
        // Inside this test the task 2 should finish first, then synchonously:
        // - task 3 should exit setup with error
        // - task 1 should be stopped as a consequence of error inside the group
        // - task 4 should be skipped
        // - the first child group of root should finish with error
        // - task 5 should be started (because of root's continueOnError policy)
        const Group root3 {
            continueOnError,
            storage,
            Group {
                parallelLimit(2),
                Group {
                    groupSetup(1),
                    createSuccessTask(1, 10ms)
                },
                Group {
                    groupSetup(2),
                    createSuccessTask(2, 1ms)
                },
                Group {
                    groupSetup(3),
                    createTaskWithSetupTweak(3, SetupResult::StopWithError)
                },
                Group {
                    groupSetup(4),
                    createSuccessTask(4)
                }
            },
            Group {
                groupSetup(5),
                createSuccessTask(5)
            }
        };
        const Log log3 {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError},
            {1, Handler::Canceled},
            {5, Handler::GroupSetup},
            {5, Handler::Setup},
            {5, Handler::Success}
        };
        QTest::newRow("NestedParallelError1")
            << TestData{storage, root1, log1, 5, DoneWith::Error};
        QTest::newRow("NestedParallelError2")
            << TestData{storage, root2, log2, 5, DoneWith::Error};
        QTest::newRow("NestedParallelError3")
            << TestData{storage, root3, log3, 5, DoneWith::Error};
    }

    {
        const Group root {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                Group {
                    parallel,
                    createSuccessTask(1)
                }
            },
            Group {
                groupSetup(2),
                Group {
                    parallel,
                    createSuccessTask(2)
                }
            },
            Group {
                groupSetup(3),
                Group {
                    parallel,
                    createSuccessTask(3)
                }
            },
            Group {
                groupSetup(4),
                Group {
                    parallel,
                    createSuccessTask(4)
                }
            }
        };
        const Log log {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {1, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {2, Handler::Success},
            {4, Handler::GroupSetup},
            {4, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Success}
        };
        QTest::newRow("DeeplyNestedParallel") << TestData{storage, root, log, 4, DoneWith::Success};
    }

    {
        const Group root {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                Group { createSuccessTask(1) }
            },
            Group {
                groupSetup(2),
                Group { createSuccessTask(2) }
            },
            Group {
                groupSetup(3),
                Group { createTaskWithSetupTweak(3, SetupResult::StopWithSuccess) }
            },
            Group {
                groupSetup(4),
                Group { createSuccessTask(4) }
            },
            Group {
                groupSetup(5),
                Group { createSuccessTask(5) }
            }
        };
        const Log log {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {1, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToSuccess},
            {4, Handler::GroupSetup},
            {4, Handler::Setup},
            {2, Handler::Success},
            {5, Handler::GroupSetup},
            {5, Handler::Setup},
            {4, Handler::Success},
            {5, Handler::Success}
        };
        QTest::newRow("DeeplyNestedParallelSuccess")
            << TestData{storage, root, log, 5, DoneWith::Success};
    }

    {
        const Group root {
            parallelLimit(2),
            storage,
            Group {
                groupSetup(1),
                Group { createSuccessTask(1) }
            },
            Group {
                groupSetup(2),
                Group { createSuccessTask(2) }
            },
            Group {
                groupSetup(3),
                Group { createTaskWithSetupTweak(3, SetupResult::StopWithError) }
            },
            Group {
                groupSetup(4),
                Group { createSuccessTask(4) }
            },
            Group {
                groupSetup(5),
                Group { createSuccessTask(5) }
            }
        };
        const Log log {
            {1, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {1, Handler::Success},
            {3, Handler::GroupSetup},
            {3, Handler::Setup},
            {3, Handler::TweakSetupToError},
            {2, Handler::Canceled}
        };
        QTest::newRow("DeeplyNestedParallelError")
            << TestData{storage, root, log, 5, DoneWith::Error};
    }

    {
        const Group root {
            storage,
            createSync(1),
            createSync(2),
            createSync(3),
            createSync(4),
            createSync(5)
        };
        const Log log {
            {1, Handler::Sync},
            {2, Handler::Sync},
            {3, Handler::Sync},
            {4, Handler::Sync},
            {5, Handler::Sync}
        };
        QTest::newRow("SyncSequential") << TestData{storage, root, log, 0, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            createSyncWithTweak(1, DoneResult::Success),
            createSyncWithTweak(2, DoneResult::Success),
            createSyncWithTweak(3, DoneResult::Success),
            createSyncWithTweak(4, DoneResult::Success),
            createSyncWithTweak(5, DoneResult::Success)
        };
        const Log log {
            {1, Handler::Sync},
            {1, Handler::TweakDoneToSuccess},
            {2, Handler::Sync},
            {2, Handler::TweakDoneToSuccess},
            {3, Handler::Sync},
            {3, Handler::TweakDoneToSuccess},
            {4, Handler::Sync},
            {4, Handler::TweakDoneToSuccess},
            {5, Handler::Sync},
            {5, Handler::TweakDoneToSuccess}
        };
        QTest::newRow("SyncWithReturn") << TestData{storage, root, log, 0, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            parallel,
            createSync(1),
            createSync(2),
            createSync(3),
            createSync(4),
            createSync(5)
        };
        const Log log {
            {1, Handler::Sync},
            {2, Handler::Sync},
            {3, Handler::Sync},
            {4, Handler::Sync},
            {5, Handler::Sync}
        };
        QTest::newRow("SyncParallel") << TestData{storage, root, log, 0, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            parallel,
            createSync(1),
            createSync(2),
            createSyncWithTweak(3, DoneResult::Error),
            createSync(4),
            createSync(5)
        };
        const Log log {
            {1, Handler::Sync},
            {2, Handler::Sync},
            {3, Handler::Sync},
            {3, Handler::TweakDoneToError}
        };
        QTest::newRow("SyncError") << TestData{storage, root, log, 0, DoneWith::Error};
    }

    {
        const Group root {
            storage,
            createSync(1),
            createSuccessTask(2),
            createSync(3),
            createSuccessTask(4),
            createSync(5),
            groupDone(0)
        };
        const Log log {
            {1, Handler::Sync},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Sync},
            {4, Handler::Setup},
            {4, Handler::Success},
            {5, Handler::Sync},
            {0, Handler::GroupSuccess}
        };
        QTest::newRow("SyncAndAsync") << TestData{storage, root, log, 2, DoneWith::Success};
    }

    {
        const Group root {
            storage,
            createSync(1),
            createSuccessTask(2),
            createSyncWithTweak(3, DoneResult::Error),
            createSuccessTask(4),
            createSync(5),
            groupDone(0)
        };
        const Log log {
            {1, Handler::Sync},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Sync},
            {3, Handler::TweakDoneToError},
            {0, Handler::GroupError}
        };
        QTest::newRow("SyncAndAsyncError") << TestData{storage, root, log, 2, DoneWith::Error};
    }

    {
        SingleBarrier barrier;

        // Test that barrier advance, triggered from inside the task described by
        // setupBarrierAdvance, placed BEFORE the group containing the waitFor() element
        // in the tree order, works OK in SEQUENTIAL mode.
        const Group root1 {
            storage,
            barrier,
            sequential,
            createBarrierAdvance(storage, barrier, 1),
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(2),
                createSuccessTask(3)
            }
        };
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::BarrierAdvance},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Success}
        };

        // Test that barrier advance, triggered from inside the task described by
        // setupTaskWithCondition, placed BEFORE the group containing the waitFor() element
        // in the tree order, works OK in PARALLEL mode.
        const Group root2 {
            storage,
            barrier,
            parallel,
            createBarrierAdvance(storage, barrier, 1),
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(2),
                createSuccessTask(3)
            }
        };
        const Log log2 {
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {1, Handler::BarrierAdvance},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Success}
        };

        // Test that barrier advance, triggered from inside the task described by
        // setupTaskWithCondition, placed AFTER the group containing the waitFor() element
        // in the tree order, works OK in PARALLEL mode.
        //
        // Notice: This won't work in SEQUENTIAL mode, since the advancing barrier, placed after the
        // group containing the WaitFor element, has no chance to be started in SEQUENTIAL mode,
        // as in SEQUENTIAL mode the next task may only be started after the previous one finished.
        // In this case, the previous task (Group element) awaits for the barrier's advance to
        // come from the not yet started next task, causing a deadlock.
        // The minimal requirement for this scenario to succeed is to set parallelLimit(2) or more.
        const Group root3 {
            storage,
            barrier,
            parallel,
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(2),
                createSuccessTask(3)
            },
            createBarrierAdvance(storage, barrier, 1)
        };
        const Log log3 {
            {2, Handler::GroupSetup},
            {1, Handler::Setup},
            {1, Handler::BarrierAdvance},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Success}
        };

        // Test that barrier advance, triggered from inside the task described by
        // setupBarrierAdvance, placed BEFORE the groups containing the waitFor() element
        // in the tree order, wakes both waitFor tasks.
        const Group root4 {
            storage,
            barrier,
            parallel,
            createBarrierAdvance(storage, barrier, 1),
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(4)
            },
            Group {
                groupSetup(3),
                waitForBarrierTask(barrier),
                createSuccessTask(5)
            }
        };
        const Log log4 {
            {1, Handler::Setup},
            {2, Handler::GroupSetup},
            {3, Handler::GroupSetup},
            {1, Handler::BarrierAdvance},
            {4, Handler::Setup},
            {5, Handler::Setup},
            {4, Handler::Success},
            {5, Handler::Success}
        };

        // Test two separate single barriers.

        SingleBarrier barrier2;

        const Group root5 {
            storage,
            barrier,
            barrier2,
            parallel,
            createBarrierAdvance(storage, barrier, 1),
            createBarrierAdvance(storage, barrier2, 2),
            Group {
                Group {
                    parallel,
                    groupSetup(1),
                    waitForBarrierTask(barrier),
                    waitForBarrierTask(barrier2)
                },
                createSuccessTask(3)
            },
        };
        const Log log5 {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {1, Handler::GroupSetup},
            {1, Handler::BarrierAdvance},
            {2, Handler::BarrierAdvance},
            {3, Handler::Setup},
            {3, Handler::Success}
        };

        // Notice the different log order for each scenario.
        QTest::newRow("BarrierSequential")
            << TestData{storage, root1, log1, 4, DoneWith::Success};
        QTest::newRow("BarrierParallelAdvanceFirst")
            << TestData{storage, root2, log2, 4, DoneWith::Success};
        QTest::newRow("BarrierParallelWaitForFirst")
            << TestData{storage, root3, log3, 4, DoneWith::Success};
        QTest::newRow("BarrierParallelMultiWaitFor")
            << TestData{storage, root4, log4, 5, DoneWith::Success};
        QTest::newRow("BarrierParallelTwoSingleBarriers")
            << TestData{storage, root5, log5, 5, DoneWith::Success};
    }

    {
        MultiBarrier<2> barrier;

        // Test that multi barrier advance, triggered from inside the tasks described by
        // setupBarrierAdvance, placed BEFORE the group containing the waitFor() element
        // in the tree order, works OK in SEQUENTIAL mode.
        const Group root1 {
            storage,
            barrier,
            sequential,
            createBarrierAdvance(storage, barrier, 1),
            createBarrierAdvance(storage, barrier, 2),
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(2),
                createSuccessTask(3)
            }
        };
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::BarrierAdvance},
            {2, Handler::Setup},
            {2, Handler::BarrierAdvance},
            {2, Handler::GroupSetup},
            {2, Handler::Setup},
            {2, Handler::Success},
            {3, Handler::Setup},
            {3, Handler::Success}
        };

        // Test that multi barrier advance, triggered from inside the tasks described by
        // setupBarrierAdvance, placed BEFORE the group containing the waitFor() element
        // in the tree order, works OK in PARALLEL mode.
        const Group root2 {
            storage,
            barrier,
            parallel,
            createBarrierAdvance(storage, barrier, 1),
            createBarrierAdvance(storage, barrier, 2),
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(3),
                createSuccessTask(4)
            }
        };
        const Log log2 {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {2, Handler::GroupSetup},
            {1, Handler::BarrierAdvance},
            {2, Handler::BarrierAdvance},
            {3, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Setup},
            {4, Handler::Success}
        };

        // Test that multi barrier advance, triggered from inside the tasks described by
        // setupBarrierAdvance, placed AFTER the group containing the waitFor() element
        // in the tree order, works OK in PARALLEL mode.
        //
        // Notice: This won't work in SEQUENTIAL mode, since the advancing barriers, placed after
        // the group containing the WaitFor element, has no chance to be started in SEQUENTIAL mode,
        // as in SEQUENTIAL mode the next task may only be started after the previous one finished.
        // In this case, the previous task (Group element) awaits for the barrier's advance to
        // come from the not yet started next task, causing a deadlock.
        // The minimal requirement for this scenario to succeed is to set parallelLimit(2) or more.
        const Group root3 {
            storage,
            barrier,
            parallel,
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(3),
                createSuccessTask(4)
            },
            createBarrierAdvance(storage, barrier, 1),
            createBarrierAdvance(storage, barrier, 2)
        };
        const Log log3 {
            {2, Handler::GroupSetup},
            {1, Handler::Setup},
            {2, Handler::Setup},
            {1, Handler::BarrierAdvance},
            {2, Handler::BarrierAdvance},
            {3, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Setup},
            {4, Handler::Success}
        };

        // Test that multi barrier advance, triggered from inside the task described by
        // setupBarrierAdvance, placed BEFORE the groups containing the waitFor() element
        // in the tree order, wakes both waitFor tasks.
        const Group root4 {
            storage,
            barrier,
            parallel,
            createBarrierAdvance(storage, barrier, 1),
            createBarrierAdvance(storage, barrier, 2),
            Group {
                groupSetup(2),
                waitForBarrierTask(barrier),
                createSuccessTask(3)
            },
            Group {
                groupSetup(3),
                waitForBarrierTask(barrier),
                createSuccessTask(4)
            }
        };
        const Log log4 {
            {1, Handler::Setup},
            {2, Handler::Setup},
            {2, Handler::GroupSetup},
            {3, Handler::GroupSetup},
            {1, Handler::BarrierAdvance},
            {2, Handler::BarrierAdvance},
            {3, Handler::Setup},
            {4, Handler::Setup},
            {3, Handler::Success},
            {4, Handler::Success}
        };

        // Notice the different log order for each scenario.
        QTest::newRow("MultiBarrierSequential")
            << TestData{storage, root1, log1, 5, DoneWith::Success};
        QTest::newRow("MultiBarrierParallelAdvanceFirst")
            << TestData{storage, root2, log2, 5, DoneWith::Success};
        QTest::newRow("MultiBarrierParallelWaitForFirst")
            << TestData{storage, root3, log3, 5, DoneWith::Success};
        QTest::newRow("MultiBarrierParallelMultiWaitFor")
            << TestData{storage, root4, log4, 6, DoneWith::Success};
    }

    {
        // Test CustomTask::withTimeout() combinations:
        // 1. When the timeout has triggered or not.
        // 2. With and without timeout handler.
        const Group root1 {
            storage,
            TestTask(setupTask(1, 1000ms), setupDone(1))
                .withTimeout(1ms)
        };
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::Canceled}
        };
        QTest::newRow("TaskErrorWithTimeout") << TestData{storage, root1, log1, 2,
                                                          DoneWith::Error};

        const Group root2 {
            storage,
            TestTask(setupTask(1, 1000ms), setupDone(1))
                .withTimeout(1ms, setupTimeout(1))
        };
        const Log log2 {
            {1, Handler::Setup},
            {1, Handler::Timeout},
            {1, Handler::Canceled}
        };
        QTest::newRow("TaskErrorWithTimeoutHandler") << TestData{storage, root2, log2, 2,
                                                                 DoneWith::Error};

        const Group root3 {
            storage,
            TestTask(setupTask(1, 1ms), setupDone(1))
                .withTimeout(1000ms)
        };
        const Log doneLog {
            {1, Handler::Setup},
            {1, Handler::Success}
        };
        QTest::newRow("TaskDoneWithTimeout") << TestData{storage, root3, doneLog, 2,
                                                         DoneWith::Success};

        const Group root4 {
            storage,
            TestTask(setupTask(1, 1ms), setupDone(1))
                .withTimeout(1000ms, setupTimeout(1))
        };
        QTest::newRow("TaskDoneWithTimeoutHandler") << TestData{storage, root4, doneLog, 2,
                                                                DoneWith::Success};
    }

    {
        // Test Group::withTimeout() combinations:
        // 1. When the timeout has triggered or not.
        // 2. With and without timeout handler.
        const Group root1 {
            storage,
            Group {
                createSuccessTask(1, 1000ms)
            }.withTimeout(1ms)
        };
        const Log log1 {
            {1, Handler::Setup},
            {1, Handler::Canceled}
        };
        QTest::newRow("GroupErrorWithTimeout") << TestData{storage, root1, log1, 2,
                                                           DoneWith::Error};

        // Test Group::withTimeout(), passing custom handler
        const Group root2 {
            storage,
            Group {
                createSuccessTask(1, 1000ms)
            }.withTimeout(1ms, setupTimeout(1))
        };
        const Log log2 {
            {1, Handler::Setup},
            {1, Handler::Timeout},
            {1, Handler::Canceled}
        };
        QTest::newRow("GroupErrorWithTimeoutHandler") << TestData{storage, root2, log2, 2,
                                                                  DoneWith::Error};

        const Group root3 {
            storage,
            Group {
                createSuccessTask(1, 1ms)
            }.withTimeout(1000ms)
        };
        const Log doneLog {
            {1, Handler::Setup},
            {1, Handler::Success}
        };
        QTest::newRow("GroupDoneWithTimeout") << TestData{storage, root3, doneLog, 2,
                                                          DoneWith::Success};

        // Test Group::withTimeout(), passing custom handler
        const Group root4 {
            storage,
            Group {
                createSuccessTask(1, 1ms)
            }.withTimeout(1000ms, setupTimeout(1))
        };
        QTest::newRow("GroupDoneWithTimeoutHandler") << TestData{storage, root4, doneLog, 2,
                                                                 DoneWith::Success};
    }

    // This test check if storage shadowing works OK.
    QTest::newRow("StorageShadowing") << storageShadowingData();
}

void tst_Tasking::testTree()
{
    QFETCH(TestData, testData);

    TaskTree taskTree({testData.root.withTimeout(1000ms)});
    QCOMPARE(taskTree.taskCount() - 1, testData.taskCount); // -1 for the timeout task above
    Log actualLog;
    const auto collectLog = [&actualLog](const CustomStorage &storage) {
        actualLog = storage.m_log;
    };
    taskTree.onStorageDone(testData.storage, collectLog);
    const DoneWith result = taskTree.runBlocking();
    QCOMPARE(taskTree.isRunning(), false);

    QCOMPARE(taskTree.progressValue(), taskTree.progressMaximum());
    QCOMPARE(actualLog, testData.expectedLog);
    QCOMPARE(CustomStorage::instanceCount(), 0);

    QCOMPARE(result, testData.onDone);
}

void tst_Tasking::testInThread_data()
{
    QTest::addColumn<TestData>("testData");
    QTest::newRow("StorageShadowing") << storageShadowingData();
    QTest::newRow("Parallel") << parallelData();
}

struct TestResult
{
    int executeCount = 0;
    ThreadResult threadResult = ThreadResult::Success;
    Log actualLog = {};
};

static const int s_loopCount = 1000;
static const int s_threadCount = 12;

static void runInThread(QPromise<TestResult> &promise, const TestData &testData)
{
    for (int i = 0; i < s_loopCount; ++i) {
        if (promise.isCanceled()) {
            promise.addResult(TestResult{i, ThreadResult::Canceled});
            return;
        }

        TaskTree taskTree({testData.root.withTimeout(1000ms)});
        if (taskTree.taskCount() - 1 != testData.taskCount) { // -1 for the timeout task above
            promise.addResult(TestResult{i, ThreadResult::FailOnTaskCountCheck});
            return;
        }
        Log actualLog;
        const auto collectLog = [&actualLog](const CustomStorage &storage) {
            actualLog = storage.m_log;
        };
        taskTree.onStorageDone(testData.storage, collectLog);

        const DoneWith result = taskTree.runBlocking(QFuture<void>(promise.future()));

        if (taskTree.isRunning()) {
            promise.addResult(TestResult{i, ThreadResult::FailOnRunningCheck});
            return;
        }
        if (taskTree.progressValue() != taskTree.progressMaximum()) {
            promise.addResult(TestResult{i, ThreadResult::FailOnProgressCheck});
            return;
        }
        if (actualLog != testData.expectedLog) {
            promise.addResult(TestResult{i, ThreadResult::FailOnLogCheck, actualLog});
            return;
        }
        if (result != testData.onDone) {
            promise.addResult(TestResult{i, ThreadResult::FailOnDoneStatusCheck});
            return;
        }
    }
    promise.addResult(TestResult{s_loopCount, ThreadResult::Success, testData.expectedLog});
}

void tst_Tasking::testInThread()
{
    QFETCH(TestData, testData);

    const auto onSetup = [testData](ConcurrentCall<TestResult> &task) {
        task.setConcurrentCallData(&runInThread, testData);
    };
    const auto onDone = [testData](const ConcurrentCall<TestResult> &task) {
        QVERIFY(task.future().resultCount());
        const TestResult result = task.result();
        QCOMPARE(result.actualLog, testData.expectedLog);
        QCOMPARE(result.threadResult, ThreadResult::Success);
        QCOMPARE(result.executeCount, s_loopCount);
    };

    QList<GroupItem> tasks = { parallel };
    for (int i = 0; i < s_threadCount; ++i)
        tasks.append(ConcurrentCallTask<TestResult>(onSetup, onDone));

    TaskTree taskTree(Group{tasks});
    const DoneWith result = taskTree.runBlocking();
    QCOMPARE(taskTree.isRunning(), false);

    QCOMPARE(CustomStorage::instanceCount(), 0);
    QCOMPARE(result, testData.onDone);
}

struct StorageIO
{
    int value = 0;
};

static Group inputOutputRecipe(const Storage<StorageIO> &storage)
{
    return Group {
        storage,
        onGroupSetup([storage] { ++storage->value; }),
        onGroupDone([storage] { storage->value *= 2; })
    };
}

void tst_Tasking::storageIO_data()
{
    QTest::addColumn<int>("input");
    QTest::addColumn<int>("output");

    QTest::newRow("-1 -> 0") << -1 << 0;
    QTest::newRow("0 -> 2") <<  0 << 2;
    QTest::newRow("1 -> 4") <<  1 << 4;
    QTest::newRow("2 -> 6") <<  2 << 6;
}

void tst_Tasking::storageIO()
{
    QFETCH(int, input);
    QFETCH(int, output);

    int actualOutput = 0;

    const Storage<StorageIO> storage;
    TaskTree taskTree(inputOutputRecipe(storage));

    const auto setInput = [input](StorageIO &storage) { storage.value = input; };
    const auto getOutput = [&actualOutput](const StorageIO &storage) { actualOutput = storage.value; };

    taskTree.onStorageSetup(storage, setInput);
    taskTree.onStorageDone(storage, getOutput);
    taskTree.runBlocking();

    QCOMPARE(taskTree.isRunning(), false);
    QCOMPARE(actualOutput, output);
}

void tst_Tasking::storageOperators()
{
    StorageBase storage1 = Storage<CustomStorage>();
    StorageBase storage2 = Storage<CustomStorage>();
    StorageBase storage3 = storage1;

    QVERIFY(storage1 == storage3);
    QVERIFY(storage1 != storage2);
    QVERIFY(storage2 != storage3);
}

// This test checks whether a running task tree may be safely destructed.
// It also checks whether the destructor of a task tree deletes properly the storage created
// while starting the task tree. When running task tree is destructed, the storage done
// handler shouldn't be invoked.
void tst_Tasking::storageDestructor()
{
    bool setupCalled = false;
    const auto setupHandler = [&setupCalled](CustomStorage &) {
        setupCalled = true;
    };
    bool doneCalled = false;
    const auto doneHandler = [&doneCalled](const CustomStorage &) {
        doneCalled = true;
    };
    QCOMPARE(CustomStorage::instanceCount(), 0);
    {
        Storage<CustomStorage> storage;
        const auto setupSleepingTask = [](TaskObject &taskObject) {
            taskObject = 1000ms;
        };
        const Group root {
            storage,
            TestTask(setupSleepingTask)
        };

        TaskTree taskTree(root);
        QCOMPARE(CustomStorage::instanceCount(), 0);
        taskTree.onStorageSetup(storage, setupHandler);
        taskTree.onStorageDone(storage, doneHandler);
        taskTree.start();
        QCOMPARE(CustomStorage::instanceCount(), 1);
    }
    QCOMPARE(CustomStorage::instanceCount(), 0);
    QVERIFY(setupCalled);
    QVERIFY(!doneCalled);
}

QTEST_GUILESS_MAIN(tst_Tasking)

#include "tst_tasking.moc"
