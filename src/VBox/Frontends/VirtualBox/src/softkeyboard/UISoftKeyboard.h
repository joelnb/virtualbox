/* $Id$ */
/** @file
 * VBox Qt GUI - UISoftKeyboard class declaration.
 */

/*
 * Copyright (C) 2016-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_softkeyboard_UISoftKeyboard_h
#define FEQT_INCLUDED_SRC_softkeyboard_UISoftKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* COM includes: */
#include "COMEnums.h"
#include "CGuest.h"
#include "CEventListener.h"

/* GUI includes: */
#include "QIManagerDialog.h"
#include "QIWithRetranslateUI.h"

/* Forward declarations: */
class UISoftKeyboardKey;
class UISoftKeyboardRow;
class QHBoxLayout;
class QVBoxLayout;
class UIToolBar;


class UISoftKeyboard : public QIWithRetranslateUI<QWidget>
{
    Q_OBJECT;

public:

    UISoftKeyboard(EmbedTo enmEmbedding, QWidget *pParent,
                                QString strMachineName = QString(), bool fShowToolbar = false);
    ~UISoftKeyboard();

protected:

    virtual void retranslateUi();
    virtual void resizeEvent(QResizeEvent *pEvent);

private slots:

private:

    void prepareObjects();
    void prepareConnections();
    void prepareToolBar();
    void saveSettings();
    void loadSettings();
    void parseLayout();
    void updateLayout();

    QHBoxLayout   *m_pMainLayout;
    QWidget       *m_pContainerWidget;
    UIToolBar     *m_pToolBar;
    const bool    m_fShowToolbar;
    QString       m_strMachineName;
    QVector<UISoftKeyboardRow*> m_rows;
    int           m_iTotalRowHeight;
    int           m_iMaxRowWidth;
};

#endif /* !FEQT_INCLUDED_SRC_softkeyboard_UISoftKeyboard_h */
