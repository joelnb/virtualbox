/* $Id$ */
/** @file
 * VBox Qt GUI - UIUserNamePasswordEditor class implementation.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
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
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILineEdit.h"
#include "UICommon.h"
#include "UIHostnameDomainNameEditor.h"
#include "UIFilePathSelector.h"
#include "UIUserNamePasswordEditor.h"
#include "UIWizardNewVM.h"
#include "UIWizardNewVMEditors.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/*********************************************************************************************************************************
*   UIUserNamePasswordGroupBox implementation.                                                                                   *
*********************************************************************************************************************************/

UIUserNamePasswordGroupBox::UIUserNamePasswordGroupBox(QWidget *pParent /* = 0 */)
    : QIWithRetranslateUI<QGroupBox>(pParent)
    , m_pUserNamePasswordEditor(0)
{
    prepare();
}

void UIUserNamePasswordGroupBox::prepare()
{
    QVBoxLayout *pUserNameContainerLayout = new QVBoxLayout(this);
    m_pUserNamePasswordEditor = new UIUserNamePasswordEditor;
    AssertReturnVoid(m_pUserNamePasswordEditor);
    m_pUserNamePasswordEditor->setLabelsVisible(true);
    m_pUserNamePasswordEditor->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    pUserNameContainerLayout->addWidget(m_pUserNamePasswordEditor);

    connect(m_pUserNamePasswordEditor, &UIUserNamePasswordEditor::sigPasswordChanged,
            this, &UIUserNamePasswordGroupBox::sigPasswordChanged);
    connect(m_pUserNamePasswordEditor, &UIUserNamePasswordEditor::sigUserNameChanged,
            this, &UIUserNamePasswordGroupBox::sigUserNameChanged);
    retranslateUi();
}

void UIUserNamePasswordGroupBox::retranslateUi()
{
    setTitle(UIWizardNewVM::tr("Username and Password"));
}

QString UIUserNamePasswordGroupBox::userName() const
{
    if (m_pUserNamePasswordEditor)
        return m_pUserNamePasswordEditor->userName();
    return QString();
}

void UIUserNamePasswordGroupBox::setUserName(const QString &strUserName)
{
    if (m_pUserNamePasswordEditor)
        m_pUserNamePasswordEditor->setUserName(strUserName);
}

QString UIUserNamePasswordGroupBox::password() const
{
    if (m_pUserNamePasswordEditor)
        return m_pUserNamePasswordEditor->password();
    return QString();
}

void UIUserNamePasswordGroupBox::setPassword(const QString &strPassword)
{
    if (m_pUserNamePasswordEditor)
        m_pUserNamePasswordEditor->setPassword(strPassword);
}

bool UIUserNamePasswordGroupBox::isComplete()
{
    if (m_pUserNamePasswordEditor)
        return m_pUserNamePasswordEditor->isComplete();
    return false;
}

void UIUserNamePasswordGroupBox::setLabelsVisible(bool fVisible)
{
    if (m_pUserNamePasswordEditor)
        m_pUserNamePasswordEditor->setLabelsVisible(fVisible);
}


/*********************************************************************************************************************************
*   UIGAInstallationGroupBox implementation.                                                                                     *
*********************************************************************************************************************************/

UIGAInstallationGroupBox::UIGAInstallationGroupBox(QWidget *pParent /* = 0 */)
    : QIWithRetranslateUI<QGroupBox>(pParent)
    , m_pGAISOPathLabel(0)
    , m_pGAISOFilePathSelector(0)

{
    prepare();
}

void UIGAInstallationGroupBox::prepare()
{
    setCheckable(true);

    QHBoxLayout *pGAInstallationISOLayout = new QHBoxLayout(this);
    AssertReturnVoid(pGAInstallationISOLayout);
    m_pGAISOPathLabel = new QLabel;
    AssertReturnVoid(m_pGAISOPathLabel);
    m_pGAISOPathLabel->setAlignment(Qt::AlignRight);
    m_pGAISOPathLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    pGAInstallationISOLayout->addWidget(m_pGAISOPathLabel);

    m_pGAISOFilePathSelector = new UIFilePathSelector;
    AssertReturnVoid(m_pGAISOFilePathSelector);

    m_pGAISOFilePathSelector->setResetEnabled(false);
    m_pGAISOFilePathSelector->setMode(UIFilePathSelector::Mode_File_Open);
    m_pGAISOFilePathSelector->setFileDialogFilters("ISO Images(*.iso *.ISO)");
    m_pGAISOFilePathSelector->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_pGAISOFilePathSelector->setInitialPath(uiCommon().defaultFolderPathForType(UIMediumDeviceType_DVD));
    if (m_pGAISOPathLabel)
        m_pGAISOPathLabel->setBuddy(m_pGAISOFilePathSelector);

    pGAInstallationISOLayout->addWidget(m_pGAISOFilePathSelector);

    connect(m_pGAISOFilePathSelector, &UIFilePathSelector::pathChanged,
            this, &UIGAInstallationGroupBox::sigPathChanged);
    connect(this, &UIGAInstallationGroupBox::toggled,
            this, &UIGAInstallationGroupBox::sltToggleWidgetsEnabled);
    retranslateUi();
}

void UIGAInstallationGroupBox::retranslateUi()
{
    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->setToolTip(UIWizardNewVM::tr("Please select an installation medium (ISO file)"));
    if (m_pGAISOPathLabel)
        m_pGAISOPathLabel->setText(UIWizardNewVM::tr("GA I&nstallation ISO:"));
    setTitle(UIWizardNewVM::tr("Gu&est Additions"));
    setToolTip(UIWizardNewVM::tr("<p>When checked the guest additions will be installed "
                                                                    "after the OS install.</p>"));
}

QString UIGAInstallationGroupBox::path() const
{
    if (m_pGAISOFilePathSelector)
        return m_pGAISOFilePathSelector->path();
    return QString();
}

void UIGAInstallationGroupBox::setPath(const QString &strPath, bool fRefreshText /* = true */)
{
    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->setPath(strPath, fRefreshText);
}

void UIGAInstallationGroupBox::mark(bool fError, const QString &strErrorMessage /* = QString() */)
{
    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->mark(fError, strErrorMessage);
}


void UIGAInstallationGroupBox::sltToggleWidgetsEnabled(bool fEnabled)
{
    if (m_pGAISOPathLabel)
        m_pGAISOPathLabel->setEnabled(fEnabled);

    if (m_pGAISOFilePathSelector)
        m_pGAISOFilePathSelector->setEnabled(m_pGAISOFilePathSelector);
}


/*********************************************************************************************************************************
*   UIAdditionalUnattendedOptions implementation.                                                                                *
*********************************************************************************************************************************/

UIAdditionalUnattendedOptions::UIAdditionalUnattendedOptions(QWidget *pParent /* = 0 */)
    :QIWithRetranslateUI<QGroupBox>(pParent)
    , m_pProductKeyLabel(0)
    , m_pProductKeyLineEdit(0)
    , m_pHostnameDomainNameEditor(0)
    , m_pStartHeadlessCheckBox(0)
{
    prepare();
}

void UIAdditionalUnattendedOptions::prepare()
{
    QGridLayout *pAdditionalOptionsContainerLayout = new QGridLayout(this);
    pAdditionalOptionsContainerLayout->setColumnStretch(0, 0);
    pAdditionalOptionsContainerLayout->setColumnStretch(1, 1);

    m_pProductKeyLabel = new QLabel;
    if (m_pProductKeyLabel)
    {
        m_pProductKeyLabel->setAlignment(Qt::AlignRight);
        m_pProductKeyLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        pAdditionalOptionsContainerLayout->addWidget(m_pProductKeyLabel, 0, 0);
    }
    m_pProductKeyLineEdit = new QILineEdit;
    if (m_pProductKeyLineEdit)
    {
        m_pProductKeyLineEdit->setInputMask(">NNNNN-NNNNN-NNNNN-NNNNN-NNNNN;#");
        if (m_pProductKeyLabel)
            m_pProductKeyLabel->setBuddy(m_pProductKeyLineEdit);
        pAdditionalOptionsContainerLayout->addWidget(m_pProductKeyLineEdit, 0, 1, 1, 2);
    }

    m_pHostnameDomainNameEditor = new UIHostnameDomainNameEditor;
    if (m_pHostnameDomainNameEditor)
        pAdditionalOptionsContainerLayout->addWidget(m_pHostnameDomainNameEditor, 1, 0, 2, 3);

    m_pStartHeadlessCheckBox = new QCheckBox;
    if (m_pStartHeadlessCheckBox)
        pAdditionalOptionsContainerLayout->addWidget(m_pStartHeadlessCheckBox, 3, 1);

    if (m_pHostnameDomainNameEditor)
        connect(m_pHostnameDomainNameEditor, &UIHostnameDomainNameEditor::sigHostnameDomainNameChanged,
                this, &UIAdditionalUnattendedOptions::sigHostnameDomainNameChanged);
    if (m_pProductKeyLineEdit)
        connect(m_pProductKeyLineEdit, &QILineEdit::textChanged,
                this, &UIAdditionalUnattendedOptions::sigProductKeyChanged);
    if (m_pStartHeadlessCheckBox)
        connect(m_pStartHeadlessCheckBox, &QCheckBox::toggled,
                this, &UIAdditionalUnattendedOptions::sigStartHeadlessChanged);


    retranslateUi();
}

void UIAdditionalUnattendedOptions::retranslateUi()
{
    setTitle(UIWizardNewVM::tr("Additional Options"));

    if (m_pProductKeyLabel)
        m_pProductKeyLabel->setText(UIWizardNewVM::tr("&Product Key:"));

    if (m_pStartHeadlessCheckBox)
    {
        m_pStartHeadlessCheckBox->setText(UIWizardNewVM::tr("&Install in Background"));
        m_pStartHeadlessCheckBox->setToolTip(UIWizardNewVM::tr("<p>When checked, the newly created virtual machine will be started "
                                                               "in headless mode (without a GUI) for the unattended guest OS install.</p>"));
    }
}

QString UIAdditionalUnattendedOptions::hostname() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->hostname();
    return QString();
}

void UIAdditionalUnattendedOptions::setHostname(const QString &strHostname)
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->setHostname(strHostname);
}

QString UIAdditionalUnattendedOptions::domainName() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->domainName();
    return QString();
}

void UIAdditionalUnattendedOptions::setDomainName(const QString &strDomainName)
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->setDomainName(strDomainName);
}

QString UIAdditionalUnattendedOptions::hostnameDomainName() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->hostnameDomainName();
    return QString();
}

bool UIAdditionalUnattendedOptions::isComplete() const
{
    if (m_pHostnameDomainNameEditor)
        return m_pHostnameDomainNameEditor->isComplete();
    return false;
}

void UIAdditionalUnattendedOptions::disableEnableProductKeyWidgets(bool fEnabled)
{
    if (m_pProductKeyLabel)
        m_pProductKeyLabel->setEnabled(fEnabled);
    if (m_pProductKeyLineEdit)
        m_pProductKeyLineEdit->setEnabled(fEnabled);
}
