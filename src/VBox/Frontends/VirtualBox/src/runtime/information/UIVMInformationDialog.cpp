/* $Id$ */
/** @file
 * VBox Qt GUI - UIVMInformationDialog class implementation.
 */

/*
 * Copyright (C) 2016-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

/* GUI includes: */
#include "QITabWidget.h"
#include "QIDialogButtonBox.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIInformationConfiguration.h"
#include "UIInformationRuntime.h"
#include "UIGuestProcessControlWidget.h"
#include "UIMachineLogic.h"
#include "UIMachine.h"
#include "UIMachineView.h"
#include "UIMachineWindow.h"
#include "UIMessageCenter.h"
#include "UIVMActivityMonitor.h"
#include "UISession.h"
#include "UIVMInformationDialog.h"
#include "VBoxUtils.h"


/* COM includes: */
#include "COMEnums.h"
#include "CMachine.h"
#include "CConsole.h"
#include "CSystemProperties.h"
#include "CMachineDebugger.h"
#include "CDisplay.h"
#include "CStorageController.h"
#include "CMediumAttachment.h"
#include "CNetworkAdapter.h"
#include "CVRDEServerInfo.h"

/* Other VBox includes: */
#include <iprt/time.h>

UIVMInformationDialog::UIVMInformationDialog(UIMachineWindow *pMachineWindow)
    : QMainWindowWithRestorableGeometryAndRetranslateUi(0)
    , m_pTabWidget(0)
    , m_pMachineWindow(pMachineWindow)
    , m_fCloseEmitted(false)
{
    /* Prepare: */
    prepare();
}

UIVMInformationDialog::~UIVMInformationDialog()
{
}

bool UIVMInformationDialog::shouldBeMaximized() const
{
    return gEDataManager->sessionInformationDialogShouldBeMaximized();
}

void UIVMInformationDialog::retranslateUi()
{
    /* Setup dialog title: */
    setWindowTitle(tr("%1 - Session Information").arg(m_pMachineWindow->machine().GetName()));

    /* Translate tabs: */
    m_pTabWidget->setTabText(0, tr("Configuration &Details"));
    m_pTabWidget->setTabText(1, tr("&Runtime Information"));
    m_pTabWidget->setTabText(2, tr("VM &Activity"));
    m_pTabWidget->setTabText(3, tr("&Guest Control"));

    /* Retranslate button box buttons: */
    if (m_pButtonBox)
    {
        m_pButtonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
        m_pButtonBox->button(QDialogButtonBox::Help)->setText(tr("Help"));
        m_pButtonBox->button(QDialogButtonBox::Close)->setStatusTip(tr("Close dialog without saving"));
        m_pButtonBox->button(QDialogButtonBox::Help)->setStatusTip(tr("Show dialog help"));
        m_pButtonBox->button(QDialogButtonBox::Close)->setShortcut(Qt::Key_Escape);
        m_pButtonBox->button(QDialogButtonBox::Help)->setShortcut(QKeySequence::HelpContents);
        m_pButtonBox->button(QDialogButtonBox::Close)->setToolTip(tr("Reset Changes (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Close)->shortcut().toString()));
        m_pButtonBox->button(QDialogButtonBox::Help)->setToolTip(tr("Show Help (%1)").arg(m_pButtonBox->button(QDialogButtonBox::Help)->shortcut().toString()));
    }
}

void UIVMInformationDialog::closeEvent(QCloseEvent *pEvent)
{
    if (!m_fCloseEmitted)
    {
        m_fCloseEmitted = true;
        UIVMInformationDialog::sigClose();
        pEvent->ignore();
    }
}

void UIVMInformationDialog::sltHandlePageChanged(int iIndex)
{
    /* Focus the browser on shown page: */
    m_pTabWidget->widget(iIndex)->setFocus();
}

void UIVMInformationDialog::sltSaveSettings()
{
    /* Save window geometry: */
    {
        const QRect geo = currentGeometry();
        LogRel2(("GUI: UIVMInformationDialog: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
                 geo.x(), geo.y(), geo.width(), geo.height()));
        gEDataManager->setSessionInformationDialogGeometry(geo, isCurrentlyMaximized());
    }
}

void UIVMInformationDialog::prepare()
{
    /* Prepare dialog: */
    prepareThis();
    /* Load settings: */
    loadSettings();
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIVMInformationDialog::sltSaveSettings);
}

void UIVMInformationDialog::prepareThis()
{
#ifdef VBOX_WS_MAC
    /* No window-icon on Mac OS X, because it acts as proxy icon which isn't necessary here. */
    setWindowIcon(QIcon());
#else
    /* Assign window-icons: */
    setWindowIcon(UIIconPool::iconSetFull(":/session_info_32px.png", ":/session_info_16px.png"));
#endif

    /* Prepare central-widget: */
    prepareCentralWidget();

    /* Retranslate: */
    retranslateUi();
}

void UIVMInformationDialog::prepareCentralWidget()
{
    /* Create central-widget: */
    setCentralWidget(new QWidget);
    AssertPtrReturnVoid(centralWidget());
    {
        /* Create main-layout: */
        new QVBoxLayout(centralWidget());
        AssertPtrReturnVoid(centralWidget()->layout());
        {
            /* Create tab-widget: */
            prepareTabWidget();
            /* Create button-box: */
            prepareButtonBox();
        }
    }
}

void UIVMInformationDialog::prepareTabWidget()
{
    /* Create tab-widget: */
    m_pTabWidget = new QITabWidget;
    AssertPtrReturnVoid(m_pTabWidget);
    {
        /* Prepare tab-widget: */
        m_pTabWidget->setTabIcon(0, UIIconPool::iconSet(":/session_info_details_16px.png"));
        m_pTabWidget->setTabIcon(1, UIIconPool::iconSet(":/session_info_runtime_16px.png"));

        /* Create Configuration Details tab: */
        UIInformationConfiguration *pInformationConfigurationWidget =
            new UIInformationConfiguration(this, m_pMachineWindow->machine(), m_pMachineWindow->console());
        if (pInformationConfigurationWidget)
        {
            m_tabs.insert(0, pInformationConfigurationWidget);
            m_pTabWidget->addTab(m_tabs.value(0), QString());
        }

        /* Create Runtime Information tab: */
        UIInformationRuntime *pInformationRuntimeWidget =
            new UIInformationRuntime(this, m_pMachineWindow->machine(), m_pMachineWindow->console(), m_pMachineWindow->uisession());
        if (pInformationRuntimeWidget)
        {
            m_tabs.insert(1, pInformationRuntimeWidget);
            m_pTabWidget->addTab(m_tabs.value(1), QString());
        }

        /* Create Performance Monitor tab: */
        UIVMActivityMonitor *pVMActivityMonitorWidget =
            new UIVMActivityMonitor(EmbedTo_Dialog, this, m_pMachineWindow->machine(),
                                     m_pMachineWindow->uisession()->actionPool());
        if (pVMActivityMonitorWidget)
        {
            connect(m_pMachineWindow->uisession(), &UISession::sigAdditionsStateChange,
                    pVMActivityMonitorWidget, &UIVMActivityMonitor::sltGuestAdditionsStateChange);
            m_tabs.insert(2, pVMActivityMonitorWidget);
            m_pTabWidget->addTab(m_tabs.value(2), QString());
        }

        /* Create Guest Process Control tab: */
        QString strMachineName;
        if (m_pMachineWindow && m_pMachineWindow->console().isOk())
        {
            CMachine comMachine = m_pMachineWindow->console().GetMachine();
            if (comMachine.isOk())
                strMachineName = comMachine.GetName();
        }
        UIGuestProcessControlWidget *pGuestProcessControlWidget =
            new UIGuestProcessControlWidget(EmbedTo_Dialog, m_pMachineWindow->console().GetGuest(),
                                            this, strMachineName, false /* fShowToolbar */);
        if (pGuestProcessControlWidget)
        {
            m_tabs.insert(3, pGuestProcessControlWidget);
            m_pTabWidget->addTab(m_tabs.value(3), QString());
        }

        /* Set Runtime Information tab as default: */
        m_pTabWidget->setCurrentIndex(2);

        /* Assign tab-widget page change handler: */
        connect(m_pTabWidget, &QITabWidget::currentChanged, this, &UIVMInformationDialog::sltHandlePageChanged);

        /* Add tab-widget into main-layout: */
        centralWidget()->layout()->addWidget(m_pTabWidget);
    }
}

void UIVMInformationDialog::prepareButtonBox()
{
    /* Create button-box: */
    m_pButtonBox = new QIDialogButtonBox;
    AssertPtrReturnVoid(m_pButtonBox);
    {
        /* Configure button-box: */
        m_pButtonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Help);
        m_pButtonBox->button(QDialogButtonBox::Close)->setShortcut(Qt::Key_Escape);
        m_pButtonBox->button(QDialogButtonBox::Help)->setShortcut(QKeySequence::HelpContents);
        uiCommon().setHelpKeyword(m_pButtonBox->button(QDialogButtonBox::Help), "vm-session-information");
        connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UIVMInformationDialog::sigClose);
        connect(m_pButtonBox->button(QDialogButtonBox::Help), &QPushButton::pressed,
                &(msgCenter()), &UIMessageCenter::sltHandleHelpRequest);
        /* add button-box into main-layout: */
        centralWidget()->layout()->addWidget(m_pButtonBox);
    }
}

void UIVMInformationDialog::loadSettings()
{
    /* Load window geometry: */
    {
        const QRect geo = gEDataManager->sessionInformationDialogGeometry(this, m_pMachineWindow);
        LogRel2(("GUI: UIVMInformationDialog: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
                 geo.x(), geo.y(), geo.width(), geo.height()));
        restoreGeometry(geo);
    }
}
