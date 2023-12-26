// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "viewer.h"
#include "recipe.h"

#include <tasking/concurrentcall.h>
#include <tasking/networkquery.h>

using namespace Tasking;

Viewer::Viewer(QWidget *parent)
    : QWidget(parent)
    , m_recipe(recipe(m_storage))
{
    setWindowTitle(tr("Data Exchange"));

    QLabel *urlLabel = new QLabel(tr("Url:"));
    m_lineEdit = new QLineEdit("https://media.licdn.com/dms/image/D4D22AQFj3ksh5rmnrg/"
                               "feedshare-shrink_800/0/1697023188446?e=1701302400&v=beta"
                               "&t=6dy5dmhzgONaLu139A6XmFSGqDohiezq1fH-q2mmu3w");
    QPushButton *startButton = new QPushButton(tr("Start"));
    QPushButton *stopButton = new QPushButton(tr("Stop"));
    QPushButton *resetButton = new QPushButton(tr("Reset"));
    m_progressBar = new QProgressBar;
    m_listWidget = new QListWidget;
    m_statusBar = new QStatusBar;

    QBoxLayout *mainLayout = new QVBoxLayout(this);
    QBoxLayout *subLayout1 = new QHBoxLayout;
    QBoxLayout *subLayout2 = new QHBoxLayout;
    subLayout1->addWidget(urlLabel);
    subLayout1->addWidget(m_lineEdit);
    subLayout2->addWidget(startButton);
    subLayout2->addWidget(stopButton);
    subLayout2->addWidget(resetButton);
    subLayout2->addWidget(m_progressBar);
    mainLayout->addLayout(subLayout1);
    mainLayout->addLayout(subLayout2);
    mainLayout->addWidget(m_listWidget);
    mainLayout->addWidget(m_statusBar);

    m_listWidget->setIconSize(QSize(s_maxSize, s_maxSize));

    const auto reset = [this] {
        m_listWidget->clear();
        m_statusBar->clearMessage();
        m_progressBar->setValue(0);
    };

    connect(startButton, &QAbstractButton::clicked, this, [this, reset] {
        reset();
        const auto setInput = [this](ExternalData &data) {
            data.inputNam = &m_nam;
            data.inputUrl = m_lineEdit->text();
            m_statusBar->showMessage(tr("Executing recipe..."));
        };

        const auto getOutput = [this](const ExternalData &data) {
            if (data.outputError) {
                m_statusBar->showMessage(*data.outputError);
                return;
            }
            m_statusBar->showMessage(tr("Recipe executed successfully."));
            for (auto it = data.outputImages.begin(); it != data.outputImages.end(); ++it) {
                m_listWidget->addItem(new QListWidgetItem(QPixmap::fromImage(it.value()),
                                                          QString("%1x%1").arg(it.key())));
            }
        };

        const auto onDone = [this] { m_taskTree.release()->deleteLater(); };

        m_taskTree.reset(new TaskTree(m_recipe));
        m_taskTree->onStorageSetup(m_storage, setInput);
        m_taskTree->onStorageDone(m_storage, getOutput);
        m_progressBar->setMaximum(m_taskTree->progressMaximum());
        QObject::connect(m_taskTree.get(), &TaskTree::progressValueChanged,
                         m_progressBar, &QProgressBar::setValue);
        QObject::connect(m_taskTree.get(), &TaskTree::done, this, onDone);
        m_taskTree->start();
    });

    connect(stopButton, &QAbstractButton::clicked, this, [this] {
        if (m_taskTree) {
            m_statusBar->showMessage(tr("Recipe stopped by user."));
            m_taskTree.reset();
        }
    });

    connect(resetButton, &QAbstractButton::clicked, this, [this, reset] {
        m_taskTree.reset();
        reset();
    });
}
