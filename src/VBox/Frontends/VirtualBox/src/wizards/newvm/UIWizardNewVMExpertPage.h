/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardNewVMExpertPage class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMExpertPage_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMExpertPage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QGroupBox;
class QRadioButton;
class QIToolButton;
class UIActionPool;
class UIAdditionalUnattendedOptions;
class UIDiskFormatsComboBox;
class UIDiskVariantWidget;
class UIMediumSizeAndPathGroupBox;
class UIGAInstallationGroupBox;
class UIMediaComboBox;
class UINameAndSystemEditor;
class UINewVMHardwareContainer;
class UIToolBox;
class UIUserNamePasswordGroupBox;

/** Expert page of the New Virtual Machine wizard. */
class UIWizardNewVMExpertPage : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVMExpertPage(UIActionPool *pActionPool);

private slots:

    void sltNameChanged(const QString &strNewName);
    void sltPathChanged(const QString &strNewPath);
    void sltOsTypeChanged();
    void sltMediaComboBoxIndexChanged();
    void sltGetWithFileOpenDialog();
    void sltISOPathChanged(const QString &strPath);
    void sltGAISOPathChanged(const QString &strPath);
    void sltOSFamilyTypeChanged(const QString &strGuestOSFamilyType);
    void sltInstallGACheckBoxToggle(bool fEnabled);
    void sltSkipUnattendedCheckBoxChecked(bool fSkip);
    void sltMediumFormatChanged();
    void sltMediumSizeChanged(qulonglong uSize);
    void sltMediumPathChanged(const QString &strPath);
    void sltMediumVariantChanged(qulonglong uVariant);
    void sltSelectedDiskSourceChanged();
    void sltMediumLocationButtonClicked();
    void sltMemorySizeChanged(int iValue);
    void sltCPUCountChanged(int iCount);
    void sltEFIEnabledChanged(bool fEnabled);
    void sltPasswordChanged(const QString &strPassword);
    void sltUserNameChanged(const QString &strUserName);
    void sltHostnameDomainNameChanged(const QString &strHostnameDomainName);
    void sltProductKeyChanged(const QString &strProductKey);
    void sltStartHeadlessChanged(bool fStartHeadless);

private:

    enum ExpertToolboxItems
    {
        ExpertToolboxItems_NameAndOSType,
        ExpertToolboxItems_Unattended,
        ExpertToolboxItems_Hardware,
        ExpertToolboxItems_Disk
    };

    /** Translation stuff. */
    void retranslateUi();

    /** Prepare stuff. */
    void createConnections();
    void initializePage();
    void initializeWidgets();
    /** Set the values of the widget if they depend on OS
      * type like recommended RAM size. The widgets whose values are
      * are explicitly modified are exempt from this. */
    void setOSTypeDependedValues();
    void cleanupPage();

    /** Validation stuff. */
    bool isComplete() const;
    bool validatePage();

    bool isProductKeyWidgetEnabled() const;
    void disableEnableUnattendedRelatedWidgets(bool fEnabled);
    void markWidgets() const;
    QWidget *createUnattendedWidgets();
    QWidget *createNewDiskWidgets();
    QWidget *createDiskWidgets();
    QWidget *createNameOSTypeWidgets();
    void updateVirtualMediumPathFromMachinePathName();
    void updateDiskWidgetsAfterMediumFormatChange();
    void updateHostnameDomainNameFromMachineName();
    void setEnableNewDiskWidgets(bool fEnable);
    void setSkipCheckBoxEnable();
    bool isUnattendedEnabled() const;
    void setEnableDiskSelectionWidgets(bool fEnabled);

    /** @name Variables
      * @{ */
        UIToolBox  *m_pToolBox;
        QGroupBox *m_pDiskFormatVariantGroupBox;
        UIDiskVariantWidget *m_pDiskVariantWidget;
        UIDiskFormatsComboBox *m_pFormatComboBox;
        UIMediumSizeAndPathGroupBox *m_pSizeAndLocationGroup;
        UINameAndSystemEditor *m_pNameAndSystemEditor;
        QCheckBox *m_pSkipUnattendedCheckBox;
        QGridLayout *m_pNameAndSystemLayout;
        UINewVMHardwareContainer *m_pHardwareWidgetContainer;
        UIAdditionalUnattendedOptions *m_pAdditionalOptionsContainer;
        UIGAInstallationGroupBox *m_pGAInstallationISOContainer;
        UIUserNamePasswordGroupBox *m_pUserNamePasswordGroupBox;
        QButtonGroup *m_pDiskSourceButtonGroup;
        QRadioButton *m_pDiskEmpty;
        QRadioButton *m_pDiskNew;
        QRadioButton *m_pDiskExisting;
        UIMediaComboBox *m_pDiskSelector;
        QIToolButton *m_pDiskSelectionButton;
        QSet<QString> m_userModifiedParameters;
        bool m_fRecommendedNoDisk;
        qulonglong m_uMediumSizeMin;
        qulonglong m_uMediumSizeMax;
        UIActionPool *m_pActionPool;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVMExpertPage_h */
