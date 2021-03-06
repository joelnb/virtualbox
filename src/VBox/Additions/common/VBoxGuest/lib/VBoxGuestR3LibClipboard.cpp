/* $Id$ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Shared Clipboard.
 */

/*
 * Copyright (C) 2007-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/clipboard-helper.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/dir.h>
# include <iprt/file.h>
# include <iprt/path.h>
#endif
#include <iprt/string.h>
#include <iprt/cpp/ministring.h>

#include "VBoxGuestR3LibInternal.h"


/**
 * Function naming convention:
 *
 * FunctionNameRecv  = Receives a host message (request).
 * FunctionNameReply = Replies to a host message (request).
 * FunctionNameSend  = Sends a guest message to the host.
 */


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/


/**
 * Connects to the Shared Clipboard service, legacy version, do not use anymore.
 *
 * @returns VBox status code
 * @param   pidClient       Where to put the client id on success. The client id
 *                          must be passed to all the other clipboard calls.
 */
VBGLR3DECL(int) VbglR3ClipboardConnect(HGCMCLIENTID *pidClient)
{
    int rc = VbglR3HGCMConnect("VBoxSharedClipboard", pidClient);
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Connects to the Shared Clipboard service, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 */
VBGLR3DECL(int) VbglR3ClipboardConnectEx(PVBGLR3SHCLCMDCTX pCtx)
{
    int rc = VbglR3ClipboardConnect(&pCtx->uClientID);
    if (RT_SUCCESS(rc))
    {
        VBoxShClConnect Msg;
        RT_ZERO(Msg);

        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                           VBOX_SHCL_GUEST_FN_CONNECT, VBOX_SHCL_CPARMS_CONNECT);

        VbglHGCMParmUInt32Set(&Msg.uProtocolVer, 0);
        VbglHGCMParmUInt32Set(&Msg.uProtocolFlags, 0);
        VbglHGCMParmUInt32Set(&Msg.cbChunkSize, 0);
        VbglHGCMParmUInt32Set(&Msg.enmCompression, 0);
        VbglHGCMParmUInt32Set(&Msg.enmChecksumType, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            rc = VbglHGCMParmUInt32Get(&Msg.uProtocolVer, &pCtx->uProtocolVer);
            if (RT_SUCCESS(rc))
                rc = VbglHGCMParmUInt32Get(&Msg.uProtocolFlags, &pCtx->uProtocolFlags);
            if (RT_SUCCESS(rc))
                rc = VbglHGCMParmUInt32Get(&Msg.cbChunkSize, &pCtx->cbChunkSize);

            /** @todo Add / handle checksum + compression type. */
        }
        else
        {
            /* If the above call fails, make sure to use some sane defaults for
             * the old (legacy) protocol. */
            pCtx->uProtocolVer   = 0;
            pCtx->uProtocolFlags = 0;
            pCtx->cbChunkSize    = _64K;

            rc = VINF_SUCCESS; /* Failing above is not fatal. */
        }

        LogFlowFunc(("uProtocolVer=%RU32, cbChunkSize=%RU32\n", pCtx->uProtocolVer, pCtx->cbChunkSize));

        LogRel2(("Shared Clipboard: Client %RU32 connected, using protocol v%RU32 (cbChunkSize=%RU32)\n",
                 pCtx->uClientID, pCtx->uProtocolVer, pCtx->cbChunkSize));

    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Disconnects from the Shared Clipboard service, legacy version, do not use anymore.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 */
VBGLR3DECL(int) VbglR3ClipboardDisconnect(HGCMCLIENTID idClient)
{
    return VbglR3HGCMDisconnect(idClient);
}


/**
 * Disconnects from the Shared Clipboard service, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 */
VBGLR3DECL(int) VbglR3ClipboardDisconnectEx(PVBGLR3SHCLCMDCTX pCtx)
{
    int rc = VbglR3ClipboardDisconnect(pCtx->uClientID);
    if (RT_SUCCESS(rc))
    {
        pCtx->uClientID = 0;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Receives reported formats from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pFormats            Where to store the received formats from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardFormatsReportRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLFORMATDATA pFormats)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pFormats, VERR_INVALID_POINTER);

    VBoxShClFormatsMsg Msg;
    RT_ZERO(Msg);

    if (pCtx->uProtocolVer >= 1)
    {
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                           VBOX_SHCL_GUEST_FN_MSG_GET, 3);

        Msg.u.v1.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
        Msg.u.v1.uFormats.SetUInt32(0);
        Msg.u.v1.fFlags.SetUInt32(0);
    }

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.u.v1.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.u.v1.uFormats.GetUInt32(&pFormats->uFormats);
        if (RT_SUCCESS(rc))
            rc = Msg.u.v1.fFlags.GetUInt32(&pFormats->fFlags);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Receives a host request to read clipboard data request from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pDataReq            Where to store the read data request from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardReadDataRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLDATAREQ pDataReq)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pDataReq, VERR_INVALID_POINTER);

    VBoxShClReadDataReqMsg Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_READ_DATA);

    Msg.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_READ_DATA);
    Msg.uFormat.SetUInt32(0);
    Msg.cbSize.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.uFormat.GetUInt32(&pDataReq->uFmt);
        if (RT_SUCCESS(rc))
            rc = Msg.cbSize.GetUInt32(&pDataReq->cbSize);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Get a host message, legacy version (protocol v0). Do not use anymore.
 *
 * Note: This is the old message which still is being used for the non-URI Shared Clipboard transfers,
 *       to not break compatibility with older additions / VBox versions.
 *
 * This will block until a message becomes available.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   pidMsg          Where to store the message id.
 * @param   pfFormats       Where to store the format(s) the message applies to.
 */
VBGLR3DECL(int) VbglR3ClipboardGetHostMsgOld(HGCMCLIENTID idClient, uint32_t *pidMsg, uint32_t *pfFormats)
{
    VBoxShClGetHostMsgOld Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient,
                       VBOX_SHCL_GUEST_FN_GET_HOST_MSG_OLD, VBOX_SHCL_CPARMS_GET_HOST_MSG_OLD);

    VbglHGCMParmUInt32Set(&Msg.msg, 0);
    VbglHGCMParmUInt32Set(&Msg.formats, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        int rc2 = VbglHGCMParmUInt32Get(&Msg.msg, pidMsg);
        if (RT_SUCCESS(rc))
        {
            rc2 = VbglHGCMParmUInt32Get(&Msg.formats, pfFormats);
            if (RT_SUCCESS(rc2))
                return rc;
        }
        rc = rc2;
    }
    *pidMsg    = UINT32_MAX - 1;
    *pfFormats = UINT32_MAX;
    return rc;
}


/**
 * Reads data from the host clipboard.
 *
 * @returns VBox status code.
 * @retval  VINF_BUFFER_OVERFLOW    If there is more data available than the caller provided buffer space for.
 *
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   fFormat         The format we're requesting the data in.
 * @param   pv              Where to store the data.
 * @param   cb              The size of the buffer pointed to by pv.
 * @param   pcb             The actual size of the host clipboard data. May be larger than cb.
 */
VBGLR3DECL(int) VbglR3ClipboardReadData(HGCMCLIENTID idClient, uint32_t fFormat, void *pv, uint32_t cb, uint32_t *pcb)
{
    VBoxShClReadDataMsg Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, VBOX_SHCL_GUEST_FN_DATA_READ, VBOX_SHCL_CPARMS_READ_DATA);
    VbglHGCMParmUInt32Set(&Msg.format, fFormat);
    VbglHGCMParmPtrSet(&Msg.ptr, pv, cb);
    VbglHGCMParmUInt32Set(&Msg.size, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        uint32_t cbActual;
        int rc2 = VbglHGCMParmUInt32Get(&Msg.size, &cbActual);
        if (RT_SUCCESS(rc2))
        {
            *pcb = cbActual;
            if (cbActual > cb)
                return VINF_BUFFER_OVERFLOW;
            return rc;
        }
        rc = rc2;
    }
    return rc;
}

/**
 * Peeks at the next host message, waiting for one to turn up.
 *
 * @returns VBox status code.
 * @retval  VERR_INTERRUPTED if interrupted.  Does the necessary cleanup, so
 *          caller just have to repeat this call.
 * @retval  VERR_VM_RESTORED if the VM has been restored (idRestoreCheck).
 *
 * @param   pCtx            Shared Clipboard command context to use for the connection.
 * @param   pidMsg          Where to store the message id.
 * @param   pcParameters    Where to store the number  of parameters which will
 *                          be received in a second call to the host.
 * @param   pidRestoreCheck Pointer to the VbglR3GetSessionId() variable to use
 *                          for the VM restore check.  Optional.
 *
 * @note    Restore check is only performed optimally with a 6.0 host.
 */
int VbglR3ClipboardMsgPeekWait(PVBGLR3SHCLCMDCTX pCtx, uint32_t *pidMsg, uint32_t *pcParameters, uint64_t *pidRestoreCheck)
{
    AssertPtrReturn(pidMsg, VERR_INVALID_POINTER);
    AssertPtrReturn(pcParameters, VERR_INVALID_POINTER);

    int rc;

    struct
    {
        VBGLIOCHGCMCALL Hdr;
        HGCMFunctionParameter idMsg;       /* Doubles as restore check on input. */
        HGCMFunctionParameter cParameters;
    } Msg;
    VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->uClientID, VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT, 2);
    VbglHGCMParmUInt64Set(&Msg.idMsg, pidRestoreCheck ? *pidRestoreCheck : 0);
    VbglHGCMParmUInt32Set(&Msg.cParameters, 0);
    rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    LogFlowFunc(("VbglR3HGCMCall -> %Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        AssertMsgReturn(   Msg.idMsg.type       == VMMDevHGCMParmType_64bit
                        && Msg.cParameters.type == VMMDevHGCMParmType_32bit,
                        ("msg.type=%d num_parms.type=%d\n", Msg.idMsg.type, Msg.cParameters.type),
                        VERR_INTERNAL_ERROR_3);

        *pidMsg       = (uint32_t)Msg.idMsg.u.value64;
        *pcParameters = Msg.cParameters.u.value32;
        return rc;
    }

    /*
     * If interrupted we must cancel the call so it doesn't prevent us from making another one.
     */
    if (rc == VERR_INTERRUPTED)
    {
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->uClientID, VBOX_SHCL_GUEST_FN_CANCEL, 0);
        int rc2 = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg.Hdr));
        AssertRC(rc2);
    }

    /*
     * If restored, update pidRestoreCheck.
     */
    if (rc == VERR_VM_RESTORED && pidRestoreCheck)
        *pidRestoreCheck = Msg.idMsg.u.value64;

    *pidMsg       = UINT32_MAX - 1;
    *pcParameters = UINT32_MAX - 2;
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Reads a root list header from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pRootListHdr        Where to store the received root list header.
 */
static int vbglR3ClipboardRootListHdrRead(PVBGLR3SHCLCMDCTX pCtx, PSHCLROOTLISTHDR pRootListHdr)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pRootListHdr, VERR_INVALID_POINTER);

    VBoxShClRootListHdrMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_READ, VBOX_SHCL_CPARMS_ROOT_LIST_HDR_READ);

    Msg.ReqParms.uContext.SetUInt32(pCtx->uContextID);
    Msg.ReqParms.fRoots.SetUInt32(0);

    Msg.cRoots.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.fRoots.GetUInt32(&pRootListHdr->fRoots); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.cRoots.GetUInt32(&pRootListHdr->cRoots); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads a root list entry from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   uIndex              Index of root list entry to read.
 * @param   pRootListEntry      Where to store the root list entry read from the host.
 */
static int vbglR3ClipboardRootListEntryRead(PVBGLR3SHCLCMDCTX pCtx, uint32_t uIndex, PSHCLROOTLISTENTRY pRootListEntry)
{
    AssertPtrReturn(pCtx,           VERR_INVALID_POINTER);
    AssertPtrReturn(pRootListEntry, VERR_INVALID_POINTER);

    VBoxShClRootListEntryMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_READ, VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_READ);

    Msg.Parms.uContext.SetUInt32(pCtx->uContextID);
    Msg.Parms.fInfo.SetUInt32(pRootListEntry->fInfo);
    Msg.Parms.uIndex.SetUInt32(uIndex);

    Msg.szName.SetPtr(pRootListEntry->pszName, pRootListEntry->cbName);
    Msg.cbInfo.SetUInt32(pRootListEntry->cbInfo);
    Msg.pvInfo.SetPtr(pRootListEntry->pvInfo, pRootListEntry->cbInfo);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.Parms.fInfo.GetUInt32(&pRootListEntry->fInfo); AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbInfo = 0;
            rc = Msg.cbInfo.GetUInt32(&cbInfo); AssertRC(rc);
            if (pRootListEntry->cbInfo != cbInfo)
                rc = VERR_INVALID_PARAMETER;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads the root list from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   ppRootList          Where to store the (allocated) root list. Must be free'd by the caller with
 *                              SharedClipboardTransferRootListFree().
 */
VBGLR3DECL(int) VbglR3ClipboardRootListRead(PVBGLR3SHCLCMDCTX pCtx, PSHCLROOTLIST *ppRootList)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(ppRootList, VERR_INVALID_POINTER);

    int rc;

    PSHCLROOTLIST pRootList = SharedClipboardTransferRootListAlloc();
    if (pRootList)
    {
        SHCLROOTLISTHDR srcRootListHdr;
        rc = vbglR3ClipboardRootListHdrRead(pCtx, &srcRootListHdr);
        if (RT_SUCCESS(rc))
        {
            pRootList->Hdr.cRoots = srcRootListHdr.cRoots;
            pRootList->Hdr.fRoots = 0; /** @todo Implement this. */

            if (srcRootListHdr.cRoots)
            {
                pRootList->paEntries =
                    (PSHCLROOTLISTENTRY)RTMemAllocZ(srcRootListHdr.cRoots * sizeof(SHCLROOTLISTENTRY));
                if (pRootList->paEntries)
                {
                    for (uint32_t i = 0; i < srcRootListHdr.cRoots; i++)
                    {
                        rc = vbglR3ClipboardRootListEntryRead(pCtx, i, &pRootList->paEntries[i]);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }

        if (RT_SUCCESS(rc))
        {
            *ppRootList = pRootList;
        }
        else
            SharedClipboardTransferRootListFree(pRootList);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a transfer status from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pEnmDir             Where to store the transfer direction for the reported transfer.
 * @param   pReport             Where to store the transfer (status) report.
 */
VBGLR3DECL(int) VbglR3ClipboarTransferStatusRecv(PVBGLR3SHCLCMDCTX pCtx,
                                                 PSHCLTRANSFERDIR pEnmDir, PSHCLTRANSFERREPORT pReport)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pReport, VERR_INVALID_POINTER);
    AssertPtrReturn(pEnmDir, VERR_INVALID_POINTER);

    VBoxShClTransferStatusMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_TRANSFER_STATUS);

    Msg.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    Msg.enmDir.SetUInt32(0);
    Msg.enmStatus.SetUInt32(0);
    Msg.rc.SetUInt32(0);
    Msg.fFlags.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt32(&pCtx->uContextID); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.enmDir.GetUInt32((uint32_t *)pEnmDir); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.enmStatus.GetUInt32(&pReport->uStatus); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.rc.GetUInt32((uint32_t *)&pReport->rc); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.fFlags.GetUInt32(&pReport->fFlags); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a transfer report from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pTransfer           Transfer of report to reply to.
 * @param   uStatus             Tranfer status to reply.
 * @param   rcTransfer          Result code (rc) to reply.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferStatusReply(PVBGLR3SHCLCMDCTX pCtx, PSHCLTRANSFER pTransfer,
                                                   SHCLTRANSFERSTATUS uStatus, int rcTransfer)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    RT_NOREF(pTransfer);

    VBoxShClReplyMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.enmType.SetUInt32(VBOX_SHCL_REPLYMSGTYPE_TRANSFER_STATUS);
    Msg.rc.SetUInt32((uint32_t )rcTransfer); /* int vs. uint32_t */
    Msg.cbPayload.SetUInt32(0);
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.TransferStatus.enmStatus.SetUInt32((uint32_t)uStatus);

    LogFlowFunc(("%s\n", VBoxShClTransferStatusToStr(uStatus)));

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a root list header from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pfRoots             Where to store the root list header flags to use, requested by the host.
 */
VBGLR3DECL(int) VbglR3ClipboardRootListHdrReadReq(PVBGLR3SHCLCMDCTX pCtx, uint32_t *pfRoots)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pfRoots, VERR_INVALID_POINTER);

    VBoxShClRootListReadReqMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_ROOT_LIST_HDR_READ_REQ);

    Msg.ReqParms.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_HDR_READ);
    Msg.ReqParms.fRoots.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt32(&pCtx->uContextID); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.fRoots.GetUInt32(pfRoots); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a root list header request.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pRootListHdr        Root lsit header to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardRootListHdrReadReply(PVBGLR3SHCLCMDCTX pCtx, PSHCLROOTLISTHDR pRootListHdr)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pRootListHdr, VERR_INVALID_POINTER);

    VBoxShClRootListHdrMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_WRITE, VBOX_SHCL_CPARMS_ROOT_LIST_HDR_WRITE);

    Msg.ReqParms.uContext.SetUInt32(pCtx->uContextID);
    Msg.ReqParms.fRoots.SetUInt32(pRootListHdr->fRoots);

    Msg.cRoots.SetUInt32(pRootListHdr->cRoots);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a root list entry from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   puIndex             Where to return the index of the root list entry the host wants to read.
 * @param   pfInfo              Where to return the read flags the host wants to use.
 */
VBGLR3DECL(int) VbglR3ClipboardRootListEntryReadReq(PVBGLR3SHCLCMDCTX pCtx, uint32_t *puIndex, uint32_t *pfInfo)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(puIndex, VERR_INVALID_POINTER);
    AssertPtrReturn(pfInfo,  VERR_INVALID_POINTER);

    VBoxShClRootListEntryReadReqMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_READ_REQ);

    Msg.Parms.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_ENTRY_READ);
    Msg.Parms.fInfo.SetUInt32(0);
    Msg.Parms.uIndex.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.Parms.uContext.GetUInt32(&pCtx->uContextID); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.Parms.fInfo.GetUInt32(pfInfo); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.Parms.uIndex.GetUInt32(puIndex); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a root list entry read request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   uIndex              Index of root list entry to reply.
 * @param   pEntry              Actual root list entry to reply.
 */
VBGLR3DECL(int) VbglR3ClipboardRootListEntryReadReply(PVBGLR3SHCLCMDCTX pCtx, uint32_t uIndex, PSHCLROOTLISTENTRY pEntry)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pEntry, VERR_INVALID_POINTER);

    VBoxShClRootListEntryMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_WRITE, VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_WRITE);

    Msg.Parms.uContext.SetUInt32(pCtx->uContextID);
    Msg.Parms.fInfo.SetUInt32(0);
    Msg.Parms.uIndex.SetUInt32(uIndex);

    Msg.szName.SetPtr(pEntry->pszName, pEntry->cbName);
    Msg.cbInfo.SetUInt32(pEntry->cbInfo);
    Msg.pvInfo.SetPtr(pEntry->pvInfo, pEntry->cbInfo);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to open a list handle to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pOpenParms          List open parameters to use for the open request.
 * @param   phList              Where to return the list handle received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardListOpenSend(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTOPENPARMS pOpenParms,
                                            PSHCLLISTHANDLE phList)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pOpenParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phList,     VERR_INVALID_POINTER);

    VBoxShClListOpenMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_LIST_OPEN, VBOX_SHCL_CPARMS_LIST_OPEN);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.fList.SetUInt32(0);
    Msg.cbFilter.SetUInt32(pOpenParms->cbFilter);
    Msg.pvFilter.SetPtr(pOpenParms->pszFilter, pOpenParms->cbFilter);
    Msg.cbPath.SetUInt32(pOpenParms->cbPath);
    Msg.pvFilter.SetPtr(pOpenParms->pszPath, pOpenParms->cbPath);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uHandle.GetUInt64(phList); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to open a list handle on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pOpenParms          Where to store the open parameters the host wants to use for opening the list handle.
 */
VBGLR3DECL(int) VbglR3ClipboardListOpenRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTOPENPARMS pOpenParms)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pOpenParms, VERR_INVALID_POINTER);

    VBoxShClListOpenMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_OPEN);

    Msg.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_OPEN);
    Msg.fList.SetUInt32(0);
    Msg.cbPath.SetUInt32(pOpenParms->cbPath);
    Msg.pvPath.SetPtr(pOpenParms->pszPath, pOpenParms->cbPath);
    Msg.cbFilter.SetUInt32(pOpenParms->cbFilter);
    Msg.pvFilter.SetPtr(pOpenParms->pszFilter, pOpenParms->cbFilter);
    Msg.uHandle.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.fList.GetUInt32(&pOpenParms->fList);
        if (RT_SUCCESS(rc))
            rc = Msg.cbFilter.GetUInt32(&pOpenParms->cbFilter);
        if (RT_SUCCESS(rc))
            rc = Msg.cbPath.GetUInt32(&pOpenParms->cbPath);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a list open request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hList               List handle of (guest) list to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardListOpenReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.enmType.SetUInt32(VBOX_SHCL_REPLYMSGTYPE_LIST_OPEN);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.cbPayload.SetUInt32(0);
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ListOpen.uHandle.SetUInt64(hList);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to close a list handle on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phList              Where to store the list handle to close, received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardListCloseRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHANDLE phList)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(phList, VERR_INVALID_POINTER);

    VBoxShClListCloseMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_CLOSE);

    Msg.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_CLOSE);
    Msg.uHandle.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.uHandle.GetUInt64(phList); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a list handle close request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hList               List handle the send the close reply for.
 */
VBGLR3DECL(int) VbglR3ClipboardListCloseReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.enmType.SetUInt32(VBOX_SHCL_REPLYMSGTYPE_LIST_CLOSE);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.cbPayload.SetUInt32(0);
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ListOpen.uHandle.SetUInt64(hList);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to close a list handle to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to request for closing on the host.
 */
VBGLR3DECL(int) VbglR3ClipboardListCloseSend(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClListCloseMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_HOST_MSG_TRANSFER_LIST_CLOSE, VBOX_SHCL_CPARMS_LIST_CLOSE);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.uHandle.SetUInt64(hList);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to read a list header to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to read list header for.
 * @param   fFlags              List header read flags to use.
 * @param   pListHdr            Where to return the list header received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardListHdrRead(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList, uint32_t fFlags,
                                           PSHCLLISTHDR pListHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pListHdr, VERR_INVALID_POINTER);

    VBoxShClListHdrMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_LIST_HDR_READ, VBOX_SHCL_CPARMS_LIST_HDR);

    Msg.ReqParms.uContext.SetUInt32(pCtx->uContextID);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fFlags.SetUInt32(fFlags);

    Msg.fFeatures.SetUInt32(0);
    Msg.cbTotalSize.SetUInt32(0);
    Msg.cTotalObjects.SetUInt64(0);
    Msg.cbTotalSize.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.fFeatures.GetUInt32(&pListHdr->fFeatures);
        if (RT_SUCCESS(rc))
            rc = Msg.cTotalObjects.GetUInt64(&pListHdr->cTotalObjects);
        if (RT_SUCCESS(rc))
            rc = Msg.cbTotalSize.GetUInt64(&pListHdr->cbTotalSize);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a list header on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phList              Where to return the list handle to read list header for.
 * @param   pfFlags             Where to return the List header read flags to use.
 */
VBGLR3DECL(int) VbglR3ClipboardListHdrReadRecvReq(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHANDLE phList, uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(phList,  VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    VBoxShClListHdrReadReqMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_HDR_READ_REQ);

    Msg.ReqParms.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_HDR_READ);
    Msg.ReqParms.uHandle.SetUInt64(0);
    Msg.ReqParms.fFlags.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.uHandle.GetUInt64(phList);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.fFlags.GetUInt32(pfFlags);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends (writes) a list header to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to write list header for.
 * @param   pListHdr            List header to write.
 */
VBGLR3DECL(int) VbglR3ClipboardListHdrWrite(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList,
                                            PSHCLLISTHDR pListHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pListHdr, VERR_INVALID_POINTER);

    VBoxShClListHdrMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_LIST_HDR_WRITE, VBOX_SHCL_CPARMS_LIST_HDR);

    Msg.ReqParms.uContext.SetUInt32(pCtx->uContextID);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fFlags.SetUInt32(0);

    Msg.fFeatures.SetUInt32(0);
    Msg.cbTotalSize.SetUInt32(pListHdr->fFeatures);
    Msg.cTotalObjects.SetUInt64(pListHdr->cTotalObjects);
    Msg.cbTotalSize.SetUInt64(pListHdr->cbTotalSize);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a reuqest to read a list entry from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to request to read a list entry for.
 * @param   pListEntry          Where to return the list entry read from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardListEntryRead(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList,
                                             PSHCLLISTENTRY pListEntry)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);

    VBoxShClListEntryMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_LIST_ENTRY_READ, VBOX_SHCL_CPARMS_LIST_ENTRY);

    Msg.ReqParms.uContext.SetUInt32(pCtx->uContextID);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fInfo.SetUInt32(0);

    Msg.szName.SetPtr(pListEntry->pszName, pListEntry->cbName);
    Msg.cbInfo.SetUInt32(pListEntry->cbInfo);
    Msg.pvInfo.SetPtr(pListEntry->pvInfo, pListEntry->cbInfo);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.cbInfo.GetUInt32(&pListEntry->cbInfo); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a list entry from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phList              Where to return the list handle to read a list entry for.
 * @param   pfInfo              Where to return the list read flags.
 */
VBGLR3DECL(int) VbglR3ClipboardListEntryReadRecvReq(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHANDLE phList, uint32_t *pfInfo)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(phList, VERR_INVALID_POINTER);
    AssertPtrReturn(pfInfo, VERR_INVALID_POINTER);

    VBoxShClListEntryReadReqMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_ENTRY_READ);

    Msg.ReqParms.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ);
    Msg.ReqParms.uHandle.SetUInt64(0);
    Msg.ReqParms.fInfo.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.uHandle.GetUInt64(phList); AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.fInfo.GetUInt32(pfInfo); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends (writes) a list entry to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to write a list etnry for.
 * @param   pListEntry          List entry to write.
 */
VBGLR3DECL(int) VbglR3ClipboardListEntryWrite(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList,
                                              PSHCLLISTENTRY pListEntry)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);

    VBoxShClListEntryMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_LIST_ENTRY_WRITE, VBOX_SHCL_CPARMS_LIST_ENTRY);

    Msg.ReqParms.uContext.SetUInt32(pCtx->uContextID);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fInfo.SetUInt32(pListEntry->fInfo);

    Msg.szName.SetPtr(pListEntry->pszName, pListEntry->cbName);
    Msg.cbInfo.SetUInt32(pListEntry->cbInfo);
    Msg.pvInfo.SetPtr(pListEntry->pvInfo, pListEntry->cbInfo);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to open an object on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pCreateParms        Where to store the object open/create parameters received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardObjOpenRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pCreateParms, VERR_INVALID_POINTER);

    VBoxShClObjOpenMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_OBJ_OPEN);

    Msg.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_OPEN);
    Msg.uHandle.SetUInt64(0);
    Msg.cbPath.SetUInt32(pCreateParms->cbPath);
    Msg.szPath.SetPtr(pCreateParms->pszPath, pCreateParms->cbPath);
    Msg.fCreate.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.cbPath.GetUInt32(&pCreateParms->cbPath);
        if (RT_SUCCESS(rc))
            rc = Msg.fCreate.GetUInt32(&pCreateParms->fCreate);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies a host request to open an object.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hObj                Object handle of opened object to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardObjOpenReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.enmType.SetUInt32(VBOX_SHCL_REPLYMSGTYPE_OBJ_OPEN);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.cbPayload.SetUInt32(0);
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ObjOpen.uHandle.SetUInt64(hObj);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends an object open request to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pCreateParms        Object open/create parameters to use for opening the object on the host.
 * @param   phObj               Where to return the object handle from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardObjOpenSend(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms,
                                           PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pCreateParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phObj,        VERR_INVALID_POINTER);

    VBoxShClObjOpenMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_OBJ_OPEN, VBOX_SHCL_CPARMS_OBJ_OPEN);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.uHandle.SetUInt64(0);
    Msg.cbPath.SetUInt32(pCreateParms->cbPath);
    Msg.szPath.SetPtr((void *)pCreateParms->pszPath, pCreateParms->cbPath + 1 /* Include terminating zero */);
    Msg.fCreate.SetUInt32(pCreateParms->fCreate);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        Msg.uHandle.GetUInt64(phObj);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to close an object on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phObj               Where to return the object handle to close from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardObjCloseRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx,  VERR_INVALID_POINTER);
    AssertPtrReturn(phObj, VERR_INVALID_POINTER);

    VBoxShClObjCloseMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_OBJ_CLOSE);

    Msg.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_CLOSE);
    Msg.uHandle.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.uHandle.GetUInt64(phObj);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to an object open request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hObj                Object handle to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardObjCloseReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.enmType.SetUInt32(VBOX_SHCL_REPLYMSGTYPE_OBJ_CLOSE);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.cbPayload.SetUInt32(0);
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ObjClose.uHandle.SetUInt64(hObj);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to close an object to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hObj                Object handle to close on the host.
 */
VBGLR3DECL(int) VbglR3ClipboardObjCloseSend(PVBGLR3SHCLCMDCTX pCtx, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClObjCloseMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_OBJ_CLOSE, VBOX_SHCL_CPARMS_OBJ_CLOSE);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.uHandle.SetUInt64(hObj);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read from an object on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phObj               Where to return the object handle to read from.
 * @param   pcbToRead           Where to return the amount (in bytes) to read.
 * @param   pfFlags             Where to return the read flags.
 */
VBGLR3DECL(int) VbglR3ClipboardObjReadRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJHANDLE phObj, uint32_t *pcbToRead,
                                           uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(phObj,     VERR_INVALID_POINTER);
    AssertPtrReturn(pcbToRead, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags,   VERR_INVALID_POINTER);

    VBoxShClObjReadReqMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_OBJ_READ_REQ);

    Msg.ReqParms.uContext.SetUInt32(VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_READ);
    Msg.ReqParms.uHandle.SetUInt64(0);
    Msg.ReqParms.cbToRead.SetUInt32(0);
    Msg.ReqParms.fRead.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt32(&pCtx->uContextID);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.uHandle.GetUInt64(phObj);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.cbToRead.GetUInt32(pcbToRead);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.fRead.GetUInt32(pfFlags);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to read from an object to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hObj                Object handle of object to read from.
 * @param   pvData              Buffer where to store the read object data.
 * @param   cbData              Size (in bytes) of buffer.
 * @param   pcbRead             Where to store the amount (in bytes) read from the object.
 */
VBGLR3DECL(int) VbglR3ClipboardObjReadSend(PVBGLR3SHCLCMDCTX pCtx, SHCLOBJHANDLE hObj,
                                           void *pvData, uint32_t cbData, uint32_t *pcbRead)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData,    VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    VBoxShClObjReadWriteMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_OBJ_READ, VBOX_SHCL_CPARMS_OBJ_READ);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.uHandle.SetUInt64(hObj);
    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cbData.SetUInt32(cbData);
    Msg.pvChecksum.SetPtr(NULL, 0);
    Msg.cbChecksum.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Add checksum support. */

        if (pcbRead)
        {
            rc = Msg.cbData.GetUInt32(pcbRead); AssertRC(rc);
            AssertReturn(cbData >= *pcbRead, VERR_TOO_MUCH_DATA);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to write to an object to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hObj                Object handle of object to write to.
 * @param   pvData              Buffer of data to write to object.
 * @param   cbData              Size (in bytes) of buffer.
 * @param   pcbWritten          Where to store the amount (in bytes) written to the object.
 */
VBGLR3DECL(int) VbglR3ClipboardObjWriteSend(PVBGLR3SHCLCMDCTX pCtx, SHCLOBJHANDLE hObj,
                                            void *pvData, uint32_t cbData, uint32_t *pcbWritten)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    /* cbData can be 0. */
    /* pcbWritten is optional. */

    VBoxShClObjReadWriteMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                       VBOX_SHCL_GUEST_FN_OBJ_WRITE, VBOX_SHCL_CPARMS_OBJ_WRITE);

    Msg.uContext.SetUInt32(pCtx->uContextID);
    Msg.uHandle.SetUInt64(hObj);
    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cbData.SetUInt32(cbData);
    Msg.pvChecksum.SetPtr(NULL, 0);
    Msg.cbChecksum.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Add checksum support. */

        if (pcbWritten)
            *pcbWritten = cbData; /** @todo For now return all as being written. */
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
*   Transfer interface implementations                                                                                           *
*********************************************************************************************************************************/

static int vbglR3ClipboardTransferIfaceGetRoots(PSHCLPROVIDERCTX pCtx, PSHCLROOTLIST *ppRootList)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardRootListRead(pCmdCtx, ppRootList);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceListOpen(PSHCLPROVIDERCTX pCtx, PSHCLLISTOPENPARMS pOpenParms,
                                                PSHCLLISTHANDLE phList)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardListOpenSend(pCmdCtx, pOpenParms, phList);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceListClose(PSHCLPROVIDERCTX pCtx, SHCLLISTHANDLE hList)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardListCloseSend(pCmdCtx, hList);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceListHdrRead(PSHCLPROVIDERCTX pCtx,
                                                   SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = SharedClipboardTransferListHdrInit(pListHdr);
    if (RT_SUCCESS(rc))
    {
        if (RT_SUCCESS(rc))
        {
            rc = VbglR3ClipboardListHdrRead(pCmdCtx, hList, 0 /* fFlags */, pListHdr);
        }
        else
            SharedClipboardTransferListHdrDestroy(pListHdr);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceListEntryRead(PSHCLPROVIDERCTX pCtx,
                                                     SHCLLISTHANDLE hList, PSHCLLISTENTRY pEntry)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardListEntryRead(pCmdCtx, hList, pEntry);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceObjOpen(PSHCLPROVIDERCTX pCtx,
                                               PSHCLOBJOPENCREATEPARMS pCreateParms, PSHCLOBJHANDLE phObj)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardObjOpenSend(pCmdCtx, pCreateParms, phObj);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceObjClose(PSHCLPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardObjCloseSend(pCmdCtx, hObj);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static int vbglR3ClipboardTransferIfaceObjRead(PSHCLPROVIDERCTX pCtx,
                                               SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData,
                                               uint32_t fFlags, uint32_t *pcbRead)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    RT_NOREF(fFlags); /* Not used yet. */

    int rc = VbglR3ClipboardObjReadSend(pCmdCtx, hObj, pvData, cbData, pcbRead);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Starts a transfer on the guest side.
 *
 * @returns VBox status code.
 * @param   pCmdCtx             Command context to use.
 * @param   pTransferCtx        Transfer context to create transfer for.
 * @param   uTransferID         ID to use for transfer to start.
 * @param   enmDir              Direction of transfer to start.
 * @param   enmSource           Source of transfer to start.
 * @param   ppTransfer          Where to return the transfer object on success. Optional.
 */
static int vbglR3ClipboardTransferStart(PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx,
                                        SHCLTRANSFERID uTransferID, SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource,
                                        PSHCLTRANSFER *ppTransfer)
{
    PSHCLTRANSFER pTransfer;
    int rc = SharedClipboardTransferCreate(&pTransfer);
    if (RT_SUCCESS(rc))
    {
        SharedClipboardTransferSetCallbacks(pTransfer, &pCmdCtx->Transfers.Callbacks);

        rc = SharedClipboardTransferCtxTransferRegisterByIndex(pTransferCtx, pTransfer, uTransferID);
        if (RT_SUCCESS(rc))
        {
            rc = SharedClipboardTransferInit(pTransfer, uTransferID, enmDir, enmSource);
            if (RT_SUCCESS(rc))
            {
                /* If this is a read transfer (reading data from host), set the interface to use
                 * our VbglR3 routines here. */
                if (enmDir == SHCLTRANSFERDIR_READ)
                {
                    SHCLPROVIDERCREATIONCTX creationCtx;
                    RT_ZERO(creationCtx);

                    creationCtx.Interface.pfnRootsGet      = vbglR3ClipboardTransferIfaceGetRoots;

                    creationCtx.Interface.pfnListOpen      = vbglR3ClipboardTransferIfaceListOpen;
                    creationCtx.Interface.pfnListClose     = vbglR3ClipboardTransferIfaceListClose;
                    creationCtx.Interface.pfnListHdrRead   = vbglR3ClipboardTransferIfaceListHdrRead;
                    creationCtx.Interface.pfnListEntryRead = vbglR3ClipboardTransferIfaceListEntryRead;

                    creationCtx.Interface.pfnObjOpen       = vbglR3ClipboardTransferIfaceObjOpen;
                    creationCtx.Interface.pfnObjClose      = vbglR3ClipboardTransferIfaceObjClose;
                    creationCtx.Interface.pfnObjRead       = vbglR3ClipboardTransferIfaceObjRead;

                    creationCtx.pvUser = pCmdCtx;

                    rc = SharedClipboardTransferSetInterface(pTransfer, &creationCtx);
                }

                if (RT_SUCCESS(rc))
                    rc = SharedClipboardTransferStart(pTransfer);
            }

            if (RT_FAILURE(rc))
                SharedClipboardTransferCtxTransferUnregister(pTransferCtx, uTransferID);
        }
    }

    if (RT_SUCCESS(rc))
    {
        if (ppTransfer)
            *ppTransfer = pTransfer;

        LogRel2(("Shared Clipboard: Transfer ID=%RU16 (%s %s) successfully started\n",
                 uTransferID,
                 enmDir    == SHCLTRANSFERDIR_READ ? "reading from" : "writing to",
                 enmSource == SHCLSOURCE_LOCAL     ? "local"        : "remote"));
    }
    else
        LogRel(("Shared Clipboard: Unable to start transfer ID=%RU16, rc=%Rrc\n", uTransferID, rc));

    /* Send a reply in any case. */
    int rc2 = VbglR3ClipboardTransferStatusReply(pCmdCtx, pTransfer,
                                                   RT_SUCCESS(rc)
                                                 ? SHCLTRANSFERSTATUS_STARTED : SHCLTRANSFERSTATUS_ERROR, rc);
    AssertRC(rc2);

    if (RT_FAILURE(rc))
    {
        SharedClipboardTransferDestroy(pTransfer);
        pTransfer = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Stops a transfer on the guest side.
 *
 * @returns VBox status code, or VERR_NOT_FOUND if transfer has not been found.
 * @param   pCmdCtx             Command context to use.
 * @param   pTransferCtx        Transfer context to stop transfer for.
 * @param   uTransferID         ID of transfer to stop.
 */
static int vbglR3ClipboardTransferStop(PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx,
                                       SHCLTRANSFERID uTransferID)
{
    int rc;

    PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx, uTransferID);
    if (pTransfer)
    {
        rc = SharedClipboardTransferClose(pTransfer);
        if (RT_SUCCESS(rc))
            rc = SharedClipboardTransferCtxTransferUnregister(pTransferCtx, uTransferID);

        if (RT_SUCCESS(rc))
        {
            LogRel2(("Shared Clipboard: Transfer ID=%RU16 successfully stopped\n", uTransferID));
        }
        else
            LogRel(("Shared Clipboard: Unable to stop transfer ID=%RU16, rc=%Rrc\n", uTransferID, rc));

        /* Send a reply in any case. */
        int rc2 = VbglR3ClipboardTransferStatusReply(pCmdCtx, pTransfer,
                                                       RT_SUCCESS(rc)
                                                     ? SHCLTRANSFERSTATUS_STOPPED : SHCLTRANSFERSTATUS_ERROR, rc);
        AssertRC(rc2);
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

VBGLR3DECL(int) VbglR3ClipboardEventGetNextEx(uint32_t idMsg, uint32_t cParms,
                                              PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx,
                                              PVBGLR3CLIPBOARDEVENT pEvent)
{
    AssertPtrReturn(pCmdCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pEvent,       VERR_INVALID_POINTER);

    LogFunc(("Handling idMsg=%RU32 (%s), cParms=%RU32\n", idMsg, VBoxShClHostMsgToStr(idMsg), cParms));

    int rc;

    switch (idMsg)
    {
        case VBOX_SHCL_HOST_MSG_TRANSFER_STATUS:
        {
            SHCLTRANSFERDIR    enmDir;
            SHCLTRANSFERREPORT transferReport;
            rc = VbglR3ClipboarTransferStatusRecv(pCmdCtx, &enmDir, &transferReport);
            if (RT_SUCCESS(rc))
            {
                const SHCLTRANSFERID uTransferID = VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID);

                LogFlowFunc(("[Transfer %RU16] enmDir=%RU32, status=%s\n",
                             uTransferID, enmDir, VBoxShClTransferStatusToStr(transferReport.uStatus)));

                switch (transferReport.uStatus)
                {
                    case SHCLTRANSFERSTATUS_INITIALIZED:
                        RT_FALL_THROUGH();
                    case SHCLTRANSFERSTATUS_STARTED:
                    {
                        SHCLSOURCE enmSource = SHCLSOURCE_INVALID;

                        /* The host announces the transfer direction from its point of view, so inverse the direction here. */
                        if (enmDir == SHCLTRANSFERDIR_WRITE)
                        {
                            enmDir = SHCLTRANSFERDIR_READ;
                            enmSource = SHCLSOURCE_REMOTE;
                        }
                        else if (enmDir == SHCLTRANSFERDIR_READ)
                        {
                            enmDir = SHCLTRANSFERDIR_WRITE;
                            enmSource = SHCLSOURCE_LOCAL;
                        }
                        else
                            AssertFailedBreakStmt(rc = VERR_INVALID_PARAMETER);

                        rc = vbglR3ClipboardTransferStart(pCmdCtx, pTransferCtx, uTransferID,
                                                          enmDir, enmSource, NULL /* ppTransfer */);
                        break;
                    }

                    case SHCLTRANSFERSTATUS_STOPPED:
                        RT_FALL_THROUGH();
                    case SHCLTRANSFERSTATUS_CANCELED:
                        RT_FALL_THROUGH();
                    case SHCLTRANSFERSTATUS_KILLED:
                        RT_FALL_THROUGH();
                    case SHCLTRANSFERSTATUS_ERROR:
                    {
                        rc = vbglR3ClipboardTransferStop(pCmdCtx, pTransferCtx,
                                                         VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                        break;
                    }

                    default:
                        rc = VERR_NOT_SUPPORTED;
                        break;
                }

                if (RT_SUCCESS(rc))
                {
                    pEvent->u.TransferStatus.enmDir = enmDir;
                    pEvent->u.TransferStatus.Report = transferReport;
                    pEvent->u.TransferStatus.uID    = VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID);

                    pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_TRANSFER_STATUS;

                    LogRel2(("Shared Clipboard: Received status %s (rc=%Rrc) for transfer ID=%RU16\n",
                             VBoxShClTransferStatusToStr(pEvent->u.TransferStatus.Report.uStatus), pEvent->u.TransferStatus.Report.rc,
                             pEvent->u.TransferStatus.uID));
                }
            }
            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_HDR_READ:
        {
            uint32_t fRoots;
            rc = VbglR3ClipboardRootListHdrReadReq(pCmdCtx, &fRoots);

            /** @todo Validate / handle fRoots. */

            if (RT_SUCCESS(rc))
            {
                PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                SHCLROOTLISTHDR rootListHdr;
                RT_ZERO(rootListHdr);

                rootListHdr.cRoots = SharedClipboardTransferRootsCount(pTransfer);

                LogFlowFunc(("cRoots=%RU32\n", rootListHdr.cRoots));

                rc = VbglR3ClipboardRootListHdrReadReply(pCmdCtx, &rootListHdr);
            }
            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_ENTRY_READ:
        {
            uint32_t uIndex;
            uint32_t fInfo;
            rc = VbglR3ClipboardRootListEntryReadReq(pCmdCtx, &uIndex, &fInfo);
            if (RT_SUCCESS(rc))
            {
                PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                SHCLROOTLISTENTRY rootListEntry;
                rc = SharedClipboardTransferRootsEntry(pTransfer, uIndex, &rootListEntry);
                if (RT_SUCCESS(rc))
                    rc = VbglR3ClipboardRootListEntryReadReply(pCmdCtx, uIndex, &rootListEntry);
            }
            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_OPEN:
        {
            SHCLLISTOPENPARMS openParmsList;
            rc = SharedClipboardTransferListOpenParmsInit(&openParmsList);
            if (RT_SUCCESS(rc))
            {
                rc = VbglR3ClipboardListOpenRecv(pCmdCtx, &openParmsList);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                    VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    LogFlowFunc(("pszPath=%s\n", openParmsList.pszPath));

                    SHCLLISTHANDLE hList = SHCLLISTHANDLE_INVALID;
                    rc = SharedClipboardTransferListOpen(pTransfer, &openParmsList, &hList);

                    /* Reply in any case. */
                    int rc2 = VbglR3ClipboardListOpenReply(pCmdCtx, rc, hList);
                    AssertRC(rc2);
                }

                SharedClipboardTransferListOpenParmsDestroy(&openParmsList);
            }

            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_CLOSE:
        {
            SHCLLISTHANDLE hList;
            rc = VbglR3ClipboardListCloseRecv(pCmdCtx, &hList);
            if (RT_SUCCESS(rc))
            {
                PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                rc = SharedClipboardTransferListClose(pTransfer, hList);

                /* Reply in any case. */
                int rc2 = VbglR3ClipboardListCloseReply(pCmdCtx, rc, hList);
                AssertRC(rc2);
            }

            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_HDR_READ:
        {
            /** @todo Handle filter + list features. */

            SHCLLISTHANDLE hList  = SHCLLISTHANDLE_INVALID;
            uint32_t                  fFlags = 0;
            rc = VbglR3ClipboardListHdrReadRecvReq(pCmdCtx, &hList, &fFlags);
            if (RT_SUCCESS(rc))
            {
                PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                SHCLLISTHDR hdrList;
                rc = SharedClipboardTransferListGetHeader(pTransfer, hList, &hdrList);
                if (RT_SUCCESS(rc))
                {
                    rc = VbglR3ClipboardListHdrWrite(pCmdCtx, hList, &hdrList);

                    SharedClipboardTransferListHdrDestroy(&hdrList);
                }
            }

            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ:
        {
            LogFlowFunc(("VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ\n"));

            SHCLLISTENTRY entryList;
            rc = SharedClipboardTransferListEntryInit(&entryList);
            if (RT_SUCCESS(rc))
            {
                SHCLLISTHANDLE hList;
                uint32_t                  fInfo;
                rc = VbglR3ClipboardListEntryReadRecvReq(pCmdCtx, &hList, &fInfo);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                    VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    rc = SharedClipboardTransferListRead(pTransfer, hList, &entryList);
                    if (RT_SUCCESS(rc))
                    {
                        PSHCLFSOBJINFO pObjInfo = (PSHCLFSOBJINFO)entryList.pvInfo;
                        Assert(entryList.cbInfo == sizeof(SHCLFSOBJINFO));

                        RT_NOREF(pObjInfo);

                        LogFlowFunc(("\t%s (%RU64 bytes)\n", entryList.pszName, pObjInfo->cbObject));

                        rc = VbglR3ClipboardListEntryWrite(pCmdCtx, hList, &entryList);
                    }
                }

                SharedClipboardTransferListEntryDestroy(&entryList);
            }

            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_OPEN:
        {
            SHCLOBJOPENCREATEPARMS openParms;
            rc = SharedClipboardTransferObjectOpenParmsInit(&openParms);
            if (RT_SUCCESS(rc))
            {
                rc = VbglR3ClipboardObjOpenRecv(pCmdCtx, &openParms);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                    VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    SHCLOBJHANDLE hObj;
                    rc = SharedClipboardTransferObjectOpen(pTransfer, &openParms, &hObj);

                    /* Reply in any case. */
                    int rc2 = VbglR3ClipboardObjOpenReply(pCmdCtx, rc, hObj);
                    AssertRC(rc2);
                }

                SharedClipboardTransferObjectOpenParmsDestroy(&openParms);
            }

            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_CLOSE:
        {
            SHCLOBJHANDLE hObj;
            rc = VbglR3ClipboardObjCloseRecv(pCmdCtx, &hObj);
            if (RT_SUCCESS(rc))
            {
                PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                rc = SharedClipboardTransferObjectClose(pTransfer, hObj);

                /* Reply in any case. */
                int rc2 = VbglR3ClipboardObjCloseReply(pCmdCtx, rc, hObj);
                AssertRC(rc2);
            }

            break;
        }

        case VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_READ:
        {
            SHCLOBJHANDLE hObj;
            uint32_t cbBuf;
            uint32_t fFlags;
            rc = VbglR3ClipboardObjReadRecv(pCmdCtx, &hObj, &cbBuf, &fFlags);
            if (RT_SUCCESS(rc))
            {
                PSHCLTRANSFER pTransfer = SharedClipboardTransferCtxGetTransfer(pTransferCtx,
                                                                                VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->uContextID));
                AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                AssertBreakStmt(pCmdCtx->cbChunkSize, rc = VERR_INVALID_PARAMETER);

                const uint32_t cbToRead = RT_MIN(cbBuf, pCmdCtx->cbChunkSize);

                LogFlowFunc(("hObj=%RU64, cbBuf=%RU32, fFlags=0x%x -> cbChunkSize=%RU32, cbToRead=%RU32\n",
                             hObj, cbBuf, fFlags, pCmdCtx->cbChunkSize, cbToRead));

                void *pvBuf = RTMemAlloc(cbToRead);
                if (pvBuf)
                {
                    uint32_t cbRead;
                    rc = SharedClipboardTransferObjectRead(pTransfer, hObj, pvBuf, cbToRead, &cbRead, fFlags);
                    if (RT_SUCCESS(rc))
                        rc = VbglR3ClipboardObjWriteSend(pCmdCtx, hObj, pvBuf, cbRead, NULL /* pcbWritten */);

                    RTMemFree(pvBuf);
                }
                else
                    rc = VERR_NO_MEMORY;
            }

            break;
        }

        default:
        {
            rc = VbglR3ClipboardEventGetNext(idMsg, cParms, pCmdCtx, pEvent);
            break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

VBGLR3DECL(int) VbglR3ClipboardEventGetNext(uint32_t idMsg, uint32_t cParms,
                                            PVBGLR3SHCLCMDCTX pCtx, PVBGLR3CLIPBOARDEVENT pEvent)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    RT_NOREF(cParms);

    int rc;

#ifdef LOG_ENABLED
    LogFunc(("Handling idMsg=%RU32 (%s), protocol v%RU32\n", idMsg, VBoxShClHostMsgToStr(idMsg), pCtx->uProtocolVer));
#endif
    switch (idMsg)
    {
        case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
        {
            rc = VbglR3ClipboardFormatsReportRecv(pCtx, &pEvent->u.ReportedFormats);
            if (RT_SUCCESS(rc))
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS;
            break;
        }

        case VBOX_SHCL_HOST_MSG_READ_DATA:
        {
            rc = VbglR3ClipboardReadDataRecv(pCtx, &pEvent->u.ReadData);
            if (RT_SUCCESS(rc))
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_READ_DATA;
            break;
        }

        case VBOX_SHCL_HOST_MSG_QUIT:
        {
            pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_QUIT;
            rc = VINF_SUCCESS;
            break;
        }

        default:
        {
            rc = VERR_NOT_SUPPORTED;
            break;
        }
    }

    if (RT_SUCCESS(rc))
    {
        /* Copy over our command context to the event. */
        pEvent->cmdCtx = *pCtx;
    }
    else
    {
        /* Report error back to the host. */
        int rc2 = VbglR3ClipboardWriteError(pCtx->uClientID, rc);
        AssertRC(rc2);

   }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Frees (destroys) a formerly allocated Shared Clipboard event.
 *
 * @returns IPRT status code.
 * @param   pEvent              Event to free (destroy).
 */
VBGLR3DECL(void) VbglR3ClipboardEventFree(PVBGLR3CLIPBOARDEVENT pEvent)
{
    if (!pEvent)
        return;

    /* Some messages require additional cleanup. */
    switch (pEvent->enmType)
    {
        default:
            break;
    }

    RTMemFree(pEvent);
    pEvent = NULL;
}

/**
 * Reports (advertises) guest clipboard formats to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                The command context returned by VbglR3ClipboardConnectEx().
 * @param   pFormats            The formats to report.
 */
VBGLR3DECL(int) VbglR3ClipboardFormatsReportEx(PVBGLR3SHCLCMDCTX pCtx, PSHCLFORMATDATA pFormats)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pFormats, VERR_INVALID_POINTER);

    VBoxShClFormatsMsg Msg;

    int rc;

    LogFlowFunc(("uFormats=0x%x\n", pFormats->uFormats));

    if (pCtx->uProtocolVer == 0)
    {
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, VBOX_SHCL_GUEST_FN_FORMATS_REPORT, 1);
        Msg.u.v0.uFormats.SetUInt32(pFormats->uFormats);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg.hdr) + sizeof(Msg.u.v0));
    }
    else
    {
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, VBOX_SHCL_GUEST_FN_FORMATS_REPORT, 3);

        Msg.u.v1.uContext.SetUInt32(pCtx->uContextID);
        Msg.u.v1.uFormats.SetUInt32(pFormats->uFormats);
        Msg.u.v1.fFlags.SetUInt32(pFormats->fFlags);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg.hdr) + sizeof(Msg.u.v1));
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reports (advertises) guest clipboard formats to the host.
 *
 * Legacy function, do not use anymore.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   fFormats        The formats to report.
 */
VBGLR3DECL(int) VbglR3ClipboardFormatsReport(HGCMCLIENTID idClient, uint32_t fFormats)
{
    AssertReturn(idClient, VERR_INVALID_PARAMETER);
    AssertReturn(fFormats, VERR_INVALID_PARAMETER);

    VBoxShClFormatsMsg Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, VBOX_SHCL_GUEST_FN_FORMATS_REPORT, 1);
    VbglHGCMParmUInt32Set(&Msg.u.v0.uFormats, fFormats);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg.hdr) + sizeof(Msg.u.v0));
}

/**
 * Sends guest clipboard data to the host. Legacy function kept for compatibility, do not use anymore.
 *
 * This is usually called in reply to a VBOX_SHCL_HOST_MSG_READ_DATA message
 * from the host.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   fFormat         The format of the data.
 * @param   pv              The data.
 * @param   cb              The size of the data.
 */
VBGLR3DECL(int) VbglR3ClipboardWriteData(HGCMCLIENTID idClient, uint32_t fFormat, void *pv, uint32_t cb)
{
    AssertReturn   (idClient, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pv,       VERR_INVALID_POINTER);
    AssertReturn   (cb,       VERR_INVALID_PARAMETER);

    VBoxShClWriteDataMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient,
                       VBOX_SHCL_GUEST_FN_DATA_WRITE, 2);

    VbglHGCMParmUInt32Set(&Msg.u.v0.format, fFormat);
    VbglHGCMParmPtrSet(&Msg.u.v0.ptr, pv, cb);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg.hdr) + sizeof(Msg.u.v0));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends guest clipboard data to the host.
 *
 * This is usually called in reply to a VBOX_SHCL_HOST_MSG_READ_DATA message
 * from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                The command context returned by VbglR3ClipboardConnectEx().
 * @param   pData               Clipboard data to send.
 */
VBGLR3DECL(int) VbglR3ClipboardWriteDataEx(PVBGLR3SHCLCMDCTX pCtx, PSHCLDATABLOCK pData)
{
    AssertPtrReturn(pCtx,  VERR_INVALID_POINTER);
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    int rc;

    if (pCtx->uProtocolVer == 0)
    {
        rc = VbglR3ClipboardWriteData(pCtx->uClientID, pData->uFormat, pData->pvData, pData->cbData);
    }
    else
    {
        VBoxShClWriteDataMsg Msg;
        RT_ZERO(Msg);

        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID,
                           VBOX_SHCL_GUEST_FN_DATA_WRITE, VBOX_SHCL_CPARMS_WRITE_DATA);

        LogFlowFunc(("CID=%RU32\n", pCtx->uContextID));

        Msg.u.v1.uContext.SetUInt32(pCtx->uContextID);
        Msg.u.v1.uFormat.SetUInt32(pData->uFormat);
        Msg.u.v1.cbData.SetUInt32(pData->cbData);
        Msg.u.v1.pvData.SetPtr(pData->pvData, pData->cbData);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Writes an error to the host.
 *
 * @returns IPRT status code.
 * @param   idClient            The client id returned by VbglR3ClipboardConnect().
 * @param   rcErr               Error (IPRT-style) to send.
 */
VBGLR3DECL(int) VbglR3ClipboardWriteError(HGCMCLIENTID idClient, int rcErr)
{
    AssertReturn(idClient, VERR_INVALID_PARAMETER);

    VBoxShClWriteErrorMsg Msg;
    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, VBOX_SHCL_GUEST_FN_ERROR, VBOX_SHCL_CPARMS_ERROR);

    /** @todo Context ID not used yet. */
    Msg.uContext.SetUInt32(0);
    Msg.rc.SetUInt32((uint32_t)rcErr); /* uint32_t vs. int. */

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    if (RT_FAILURE(rc))
        LogFlowFunc(("Sending error %Rrc failed with rc=%Rrc\n", rcErr, rc));
    if (rc == VERR_NOT_SUPPORTED)
        rc = VINF_SUCCESS;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reporting error %Rrc to the host failed with %Rrc\n", rcErr, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

