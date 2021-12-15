/* $Id$ */
/** @file
 * VBox Qt GUI - UIFileManagerGuestTable class declaration.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIFileManagerGuestTable_h
#define FEQT_INCLUDED_SRC_guestctrl_UIFileManagerGuestTable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
# include <QUuid>

/* COM includes: */
#include "COMEnums.h"
#include "CEventListener.h"
#include "CEventSource.h"
#include "CGuest.h"
#include "CGuestSession.h"
#include "CMachine.h"
#include "CSession.h"
#include "CConsole.h"


/* GUI includes: */
#include "UIFileManagerTable.h"
#include "UIMainEventListener.h"

/* Forward declarations: */
class CGuestSessionStateChangedEvent;
class UIActionPool;
class UICustomFileSystemItem;
class UIGuestSessionCreateWidget;

/** This class scans the guest file system by using the VBox Guest Control API
 *  and populates the UIGuestControlFileModel*/
class UIFileManagerGuestTable : public UIFileManagerTable
{
    Q_OBJECT;

signals:

    void sigNewFileOperation(const CProgress &comProgress, const QString &strTableName);

public:

    UIFileManagerGuestTable(UIActionPool *pActionPool, const CMachine &comMachine, QWidget *pParent = 0);
    ~UIFileManagerGuestTable();
    void copyGuestToHost(const QString& hostDestinationPath);
    void copyHostToGuest(const QStringList &hostSourcePathList,
                         const QString &strDestination = QString());
    QUuid machineId();
    bool isGuestSessionRunning() const;

protected:

    void            retranslateUi() override final;
    virtual void    readDirectory(const QString& strPath, UICustomFileSystemItem *parent, bool isStartDir = false) override final;
    virtual void    deleteByItem(UICustomFileSystemItem *item) override final;
    virtual void    deleteByPath(const QStringList &pathList) override final;
    virtual void    goToHomeDirectory() override final;
    virtual bool    renameItem(UICustomFileSystemItem *item, QString newBaseName) override final;
    virtual bool    createDirectory(const QString &path, const QString &directoryName) override final;
    virtual QString fsObjectPropertyString() override final;
    virtual void    showProperties() override final;
    virtual void    determineDriveLetters() override final;
    virtual void    determinePathSeparator() override final;
    virtual void    prepareToolbar() override final;
    virtual void    createFileViewContextMenu(const QWidget *pWidget, const QPoint &point) override final;
    /** @name Copy/Cut guest-to-guest stuff.
     * @{ */
        /** Disable/enable paste action depending on the m_eFileOperationType. */
        virtual void  setPasteActionEnabled(bool fEnabled) override final;
        virtual void  pasteCutCopiedObjects() override final;
    /** @} */
    virtual void  setState();
    virtual void  setSessionDependentWidgetsEnabled();

private slots:

    void sltGuestSessionPanelToggled(bool fChecked);
    void sltGuestSessionUnregistered(CGuestSession guestSession);
    void sltGuestSessionRegistered(CGuestSession guestSession);
    void sltGuestSessionStateChanged(const CGuestSessionStateChangedEvent &cEvent);
    void sltCreateGuestSession(QString strUserName, QString strPassword);
    void sltHandleCloseSessionRequest();
    void sltMachineStateChange(const QUuid &uMachineId, const KMachineState state);
    void sltCommitDataSignalReceived();
    void sltAdditionsStateChange();

private:

    enum State
    {
        State_InvalidMachineReference,
        State_MachineNotRunning,
        State_NoGuestAdditions,
        State_SessionPossible,
        State_SessionRunning,
        State_Max
    };

    KFsObjType  fileType(const CFsObjInfo &fsInfo);
    KFsObjType  fileType(const CGuestFsObjInfo &fsInfo);

    void prepareActionConnections();
    bool checkGuestSession();
    QString permissionString(const CFsObjInfo &fsInfo);
    bool isFileObjectHidden(const CFsObjInfo &fsInfo);

    void prepareListener(ComObjPtr<UIMainEventListenerImpl> &Qtistener,
                         CEventListener &comEventListener,
                         CEventSource comEventSource, QVector<KVBoxEventType>& eventTypes);

    void cleanupListener(ComObjPtr<UIMainEventListenerImpl> &QtListener,
                         CEventListener &comEventListener,
                         CEventSource comEventSource);
    void cleanupGuestListener();
    void cleanupGuestSessionListener();
    void cleanupConsoleListener();
    void prepareGuestSessionPanel();
    bool openGuestSession(const QString& strUserName, const QString& strPassword);
    void closeGuestSession();
    bool openMachineSession();
    bool closeMachineSession();
    bool isGuestAdditionsAvailable();
    void setStateAndEnableWidgets();

    void initFileTable();
    void cleanAll();

    CGuest          m_comGuest;
    CGuestSession   m_comGuestSession;
    CSession        m_comSession;
    CMachine        m_comMachine;
    CConsole        m_comConsole;

    ComObjPtr<UIMainEventListenerImpl> m_pQtGuestListener;
    ComObjPtr<UIMainEventListenerImpl> m_pQtSessionListener;
    ComObjPtr<UIMainEventListenerImpl> m_pQtConsoleListener;
    CEventListener m_comSessionListener;
    CEventListener m_comGuestListener;
    CEventListener m_comConsoleListener;
    UIGuestSessionCreateWidget *m_pGuestSessionPanel;
;
    State m_enmState;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIFileManagerGuestTable_h */
