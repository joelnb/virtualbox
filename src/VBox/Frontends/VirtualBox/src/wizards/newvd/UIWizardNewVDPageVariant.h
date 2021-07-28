/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardNewVDPageVariant class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDPageVariant_h
#define FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDPageVariant_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizardPage.h"

/* Forward declarations: */
class CMediumFormat;
class QButtonGroup;
class QCheckBox;
class QIRichTextLabel;
class UIDiskVariantGroupBox;

// /* 2nd page of the New Virtual Hard Drive wizard (base part): */
// class SHARED_LIBRARY_STUFF UIWizardNewVDPageBaseVariant : public UIWizardPageBase
// {
// protected:

//     /** Constructor: */
//     UIWizardNewVDPageBaseVariant();

//     QWidget *createMediumVariantWidgets(bool fWithLabels);
//     qulonglong mediumVariant() const;
//     void setMediumVariant(qulonglong uMediumVariant);
//     void retranslateWidgets();
//     /** Check Medium format capability and decide if certain widgets can be shown. */
//     void setWidgetVisibility(CMediumFormat &mediumFormat);
//     void updateMediumVariantWidgetsAfterFormatChange(const CMediumFormat &mediumFormat);

// };

class SHARED_LIBRARY_STUFF UIWizardNewVDPageVariant : public UINativeWizardPage
{
    Q_OBJECT;

public:

    UIWizardNewVDPageVariant();

private:

    void retranslateUi();
    void initializePage();
    bool isComplete() const;
    void prepare();
    void setWidgetVisibility(const CMediumFormat &mediumFormat);

    QIRichTextLabel *m_pDescriptionLabel;
    QIRichTextLabel *m_pDynamicLabel;
    QIRichTextLabel *m_pFixedLabel;
    QIRichTextLabel *m_pSplitLabel;
    UIDiskVariantGroupBox *m_pVariantGroupBox;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_newvd_UIWizardNewVDPageVariant_h */
