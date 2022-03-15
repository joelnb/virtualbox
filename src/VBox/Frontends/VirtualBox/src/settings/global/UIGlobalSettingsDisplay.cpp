/* $Id$ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsDisplay class implementation.
 */

/*
 * Copyright (C) 2012-2022 Oracle Corporation
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
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIGlobalDisplayFeaturesEditor.h"
#include "UIGlobalSettingsDisplay.h"
#include "UIMaximumGuestScreenSizeEditor.h"
#include "UIMessageCenter.h"
#include "UIScaleFactorEditor.h"


/** Global settings: Display page data structure. */
struct UIDataSettingsGlobalDisplay
{
    /** Constructs data. */
    UIDataSettingsGlobalDisplay()
        : m_fActivateHoveredMachineWindow(false)
        , m_fDisableHostScreenSaver(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalDisplay &other) const
    {
        return    true
               && (m_guiMaximumGuestScreenSizeValue == other.m_guiMaximumGuestScreenSizeValue)
               && (m_scaleFactors == other.m_scaleFactors)
               && (m_fActivateHoveredMachineWindow == other.m_fActivateHoveredMachineWindow)
               && (m_fDisableHostScreenSaver == other.m_fDisableHostScreenSaver)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalDisplay &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalDisplay &other) const { return !equal(other); }

    /** Holds the maximum guest-screen size value. */
    UIMaximumGuestScreenSizeValue  m_guiMaximumGuestScreenSizeValue;
    /** Holds the guest screen scale-factor. */
    QList<double>                  m_scaleFactors;
    /** Holds whether we should automatically activate machine window under the mouse cursor. */
    bool                           m_fActivateHoveredMachineWindow;
    /** Holds whether we should disable host sceen saver on a vm is running. */
    bool                           m_fDisableHostScreenSaver;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsDisplay implementation.                                                                                *
*********************************************************************************************************************************/

UIGlobalSettingsDisplay::UIGlobalSettingsDisplay()
    : m_pCache(0)
    , m_pEditorMaximumGuestScreenSize(0)
    , m_pEditorScaleFactor(0)
    , m_pEditorGlobalDisplayFeatures(0)
{
    prepare();
}

UIGlobalSettingsDisplay::~UIGlobalSettingsDisplay()
{
    cleanup();
}

void UIGlobalSettingsDisplay::loadToCacheFrom(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalDisplay oldData;
    oldData.m_guiMaximumGuestScreenSizeValue = UIMaximumGuestScreenSizeValue(gEDataManager->maxGuestResolutionPolicy(),
                                                                             gEDataManager->maxGuestResolutionForPolicyFixed());
    oldData.m_scaleFactors = gEDataManager->scaleFactors(UIExtraDataManager::GlobalID);
    oldData.m_fActivateHoveredMachineWindow = gEDataManager->activateHoveredMachineWindow();
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_X11)
    oldData.m_fDisableHostScreenSaver = gEDataManager->disableHostScreenSaver();
#endif /* VBOX_WS_WIN || VBOX_WS_X11 */
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsDisplay::getFromCache()
{
    /* Load old data from cache: */
    const UIDataSettingsGlobalDisplay &oldData = m_pCache->base();
    m_pEditorMaximumGuestScreenSize->setValue(oldData.m_guiMaximumGuestScreenSizeValue);
    m_pEditorScaleFactor->setScaleFactors(oldData.m_scaleFactors);
    m_pEditorScaleFactor->setMonitorCount(gpDesktop->screenCount());
    m_pEditorGlobalDisplayFeatures->setActivateOnMouseHover(oldData.m_fActivateHoveredMachineWindow);
    m_pEditorGlobalDisplayFeatures->setDisableHostScreenSaver(oldData.m_fDisableHostScreenSaver);
}

void UIGlobalSettingsDisplay::putToCache()
{
    /* Prepare new data: */
    UIDataSettingsGlobalDisplay newData = m_pCache->base();

    /* Cache new data: */
    newData.m_guiMaximumGuestScreenSizeValue = m_pEditorMaximumGuestScreenSize->value();
    newData.m_scaleFactors = m_pEditorScaleFactor->scaleFactors();
    newData.m_fActivateHoveredMachineWindow = m_pEditorGlobalDisplayFeatures->activateOnMouseHover();
    newData.m_fDisableHostScreenSaver = m_pEditorGlobalDisplayFeatures->disableHostScreenSaver();
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsDisplay::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsDisplay::retranslateUi()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorMaximumGuestScreenSize->minimumLabelHorizontalHint());
    iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorScaleFactor->minimumLabelHorizontalHint());
    iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorGlobalDisplayFeatures->minimumLabelHorizontalHint());
    m_pEditorMaximumGuestScreenSize->setMinimumLayoutIndent(iMinimumLayoutHint);
    m_pEditorScaleFactor->setMinimumLayoutIndent(iMinimumLayoutHint);
    m_pEditorGlobalDisplayFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);
}

void UIGlobalSettingsDisplay::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalDisplay;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    retranslateUi();
}

void UIGlobalSettingsDisplay::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare maximum guest screen size editor: */
        m_pEditorMaximumGuestScreenSize = new UIMaximumGuestScreenSizeEditor(this);
        if (m_pEditorMaximumGuestScreenSize)
            pLayout->addWidget(m_pEditorMaximumGuestScreenSize);

        /* Prepare scale-factor editor: */
        m_pEditorScaleFactor = new UIScaleFactorEditor(this, true /* with label */);
        if (m_pEditorScaleFactor)
            pLayout->addWidget(m_pEditorScaleFactor);

        /* Prepare global display features editor: */
        m_pEditorGlobalDisplayFeatures = new UIGlobalDisplayFeaturesEditor(this);
        if (m_pEditorGlobalDisplayFeatures)
            pLayout->addWidget(m_pEditorGlobalDisplayFeatures);

        /* Add stretch to the end: */
        pLayout->addStretch();
    }
}

void UIGlobalSettingsDisplay::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsDisplay::saveData()
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Save display settings from cache: */
    if (   fSuccess
        && m_pCache->wasChanged())
    {
        /* Get old data from cache: */
        const UIDataSettingsGlobalDisplay &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalDisplay &newData = m_pCache->data();

        /* Save maximum guest screen size and policy: */
        if (   fSuccess
            && newData.m_guiMaximumGuestScreenSizeValue != oldData.m_guiMaximumGuestScreenSizeValue)
            /* fSuccess = */ gEDataManager->setMaxGuestScreenResolution(newData.m_guiMaximumGuestScreenSizeValue.m_enmPolicy,
                                                                        newData.m_guiMaximumGuestScreenSizeValue.m_size);
        /* Save guest-screen scale-factor: */
        if (   fSuccess
            && newData.m_scaleFactors != oldData.m_scaleFactors)
            /* fSuccess = */ gEDataManager->setScaleFactors(newData.m_scaleFactors, UIExtraDataManager::GlobalID);
        /* Save whether hovered machine-window should be activated automatically: */
        if (   fSuccess
            && newData.m_fActivateHoveredMachineWindow != oldData.m_fActivateHoveredMachineWindow)
            /* fSuccess = */ gEDataManager->setActivateHoveredMachineWindow(newData.m_fActivateHoveredMachineWindow);
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_X11)
        /* Save whether the host screen saver is to be disable when a vm is running: */
        if (   fSuccess
            && newData.m_fDisableHostScreenSaver != oldData.m_fDisableHostScreenSaver)
            /* fSuccess = */ gEDataManager->setDisableHostScreenSaver(newData.m_fDisableHostScreenSaver);
#endif /* VBOX_WS_WIN || VBOX_WS_X11 */
    }
    /* Return result: */
    return fSuccess;
}
