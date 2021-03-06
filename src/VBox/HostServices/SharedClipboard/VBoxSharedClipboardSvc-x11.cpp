/* $Id$ */
/** @file
 * Shared Clipboard Service - Linux host.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <iprt/errcore.h>

#include "VBoxSharedClipboardSvc-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Global context information used by the host glue for the X11 clipboard backend.
 */
struct _SHCLCONTEXT
{
    /** This mutex is grabbed during any critical operations on the clipboard
     * which might clash with others. */
    RTCRITSECT           CritSect;
    /** Pointer to the opaque X11 backend structure */
    CLIPBACKEND         *pBackend;
    /** Pointer to the VBox host client data structure. */
    PSHCLCLIENT pClient;
    /** We set this when we start shutting down as a hint not to post any new
     * requests. */
    bool                 fShuttingDown;
};


/**
 * Report formats available in the X11 clipboard to VBox.
 * @param  pCtx                 Opaque context pointer for the glue code.
 * @param  u32Formats           The formats available.
 */
DECLCALLBACK(void) ClipReportX11FormatsCallback(SHCLCONTEXT *pCtx, uint32_t u32Formats)
{
    LogFlowFunc(("pCtx=%p, u32Formats=%02X\n", pCtx, u32Formats));

    if (u32Formats == VBOX_SHCL_FMT_NONE) /* No formats to report? Bail out early. */
        return;

    SHCLFORMATDATA formatData;
    RT_ZERO(formatData);

    formatData.uFormats = u32Formats;

    int rc2 = shclSvcFormatsReport(pCtx->pClient, &formatData);
    AssertRC(rc2);
}

int ShClSvcImplInit(void)
{
    LogFlowFuncEnter();
    return VINF_SUCCESS;
}

void ShClSvcImplDestroy(void)
{
    LogFlowFuncEnter();
}

/**
 * @note  On the host, we assume that some other application already owns
 *        the clipboard and leave ownership to X11.
 */
int ShClSvcImplConnect(PSHCLCLIENT pClient, bool fHeadless)
{
    int rc = VINF_SUCCESS;

    PSHCLCONTEXT pCtx = (PSHCLCONTEXT)RTMemAllocZ(sizeof(SHCLCONTEXT));
    if (pCtx)
    {
        RTCritSectInit(&pCtx->CritSect);
        CLIPBACKEND *pBackend = ClipConstructX11(pCtx, fHeadless);
        if (!pBackend)
        {
            rc = VERR_NO_MEMORY;
        }
        else
        {
            pCtx->pBackend = pBackend;
            pClient->State.pCtx = pCtx;
            pCtx->pClient = pClient;

            rc = ClipStartX11(pBackend, true /* grab shared clipboard */);
            if (RT_FAILURE(rc))
                ClipDestructX11(pBackend);
        }

        if (RT_FAILURE(rc))
        {
            RTCritSectDelete(&pCtx->CritSect);
            RTMemFree(pCtx);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClSvcImplSync(PSHCLCLIENT pClient)
{
    LogFlowFuncEnter();

    /* Tell the guest we have no data in case X11 is not available.  If
     * there is data in the host clipboard it will automatically be sent to
     * the guest when the clipboard starts up. */
    SHCLFORMATDATA formatData;
    RT_ZERO(formatData);

    formatData.uFormats = VBOX_SHCL_FMT_NONE;

    return shclSvcFormatsReport(pClient, &formatData);
}

/**
 * Shut down the shared clipboard service and "disconnect" the guest.
 * @note  Host glue code
 */
int ShClSvcImplDisconnect(PSHCLCLIENT pClient)
{
    LogFlowFuncEnter();

    PSHCLCONTEXT pCtx = pClient->State.pCtx;
    AssertPtr(pCtx);

    /* Drop the reference to the client, in case it is still there.  This
     * will cause any outstanding clipboard data requests from X11 to fail
     * immediately. */
    pCtx->fShuttingDown = true;

    int rc = ClipStopX11(pCtx->pBackend);
    /** @todo handle this slightly more reasonably, or be really sure
     *        it won't go wrong. */
    AssertRC(rc);

    if (RT_SUCCESS(rc))  /* And if not? */
    {
        ClipDestructX11(pCtx->pBackend);
        RTCritSectDelete(&pCtx->CritSect);
        RTMemFree(pCtx);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClSvcImplFormatAnnounce(PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx,
                              PSHCLFORMATDATA pFormats)
{
    RT_NOREF(pCmdCtx);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (pFormats->uFormats & VBOX_SHCL_FMT_URI_LIST) /* No transfer support yet. */
        return VINF_SUCCESS;
#endif

    ClipAnnounceFormatToX11(pClient->State.pCtx->pBackend, pFormats->uFormats);

    return VINF_SUCCESS;
}

/** Structure describing a request for clipoard data from the guest. */
struct _CLIPREADCBREQ
{
    /** Where to write the returned data to. Weak pointer! */
    void                *pv;
    /** The size of the buffer in pv. */
    uint32_t             cb;
    /** The actual size of the data written. */
    uint32_t            *pcbActual;
    /** The request's event ID. */
    SHCLEVENTID uEvent;
};

/**
 * @note   We always fail or complete asynchronously.
 * @note   On success allocates a CLIPREADCBREQ structure which must be
 *         freed in ClipCompleteDataRequestFromX11 when it is called back from
 *         the backend code.
 */
int ShClSvcImplReadData(PSHCLCLIENT pClient,
                        PSHCLCLIENTCMDCTX pCmdCtx, PSHCLDATABLOCK pData, uint32_t *pcbActual)
{
    RT_NOREF(pCmdCtx);

    LogFlowFunc(("pClient=%p, uFormat=%02X, pv=%p, cb=%u, pcbActual=%p\n",
                 pClient, pData->uFormat, pData->pvData, pData->cbData, pcbActual));

    int rc = VINF_SUCCESS;

    CLIPREADCBREQ *pReq = (CLIPREADCBREQ *)RTMemAllocZ(sizeof(CLIPREADCBREQ));
    if (pReq)
    {
        const SHCLEVENTID uEvent = SharedClipboardEventIDGenerate(&pClient->Events);

        pReq->pv        = pData->pvData;
        pReq->cb        = pData->cbData;
        pReq->pcbActual = pcbActual;
        pReq->uEvent    = uEvent;

        rc = ClipRequestDataFromX11(pClient->State.pCtx->pBackend, pData->uFormat, pReq);
        if (RT_SUCCESS(rc))
        {
            rc = SharedClipboardEventRegister(&pClient->Events, uEvent);
            if (RT_SUCCESS(rc))
            {
                PSHCLEVENTPAYLOAD pPayload;
                rc = SharedClipboardEventWait(&pClient->Events, uEvent, 30 * 1000, &pPayload);
                if (RT_SUCCESS(rc))
                {
                    memcpy(pData->pvData,  pPayload->pvData, RT_MIN(pData->cbData, pPayload->cbData));
                    pData->cbData = pPayload->cbData;

                    Assert(pData->cbData == pPayload->cbData); /* Sanity. */
                }

                SharedClipboardEventUnregister(&pClient->Events, uEvent);
            }
        }
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int ShClSvcImplWriteData(PSHCLCLIENT pClient,
                         PSHCLCLIENTCMDCTX pCmdCtx, PSHCLDATABLOCK pData)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pCmdCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pData,   VERR_INVALID_POINTER);

    LogFlowFunc(("pClient=%p, pv=%p, cb=%RU32, uFormat=%02X\n",
                 pClient, pData->pvData, pData->cbData, pData->uFormat));

    int rc = shclSvcDataReadSignal(pClient, pCmdCtx, pData);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Completes a request from the host service for reading the X11 clipboard data.
 * The data should be written to the buffer provided in the initial request.
 *
 * @param  pCtx                 Request context information.
 * @param  rc                   The completion status of the request.
 * @param  pReq                 Request.
 * @param  pv                   Address.
 * @param  cb                   Size.
 *
 * @todo   Change this to deal with the buffer issues rather than offloading them onto the caller.
 */
void ClipRequestFromX11CompleteCallback(SHCLCONTEXT *pCtx, int rc,
                                        CLIPREADCBREQ *pReq, void *pv, uint32_t cb)
{
    AssertMsgRC(rc, ("Clipboard data completion from X11 failed with %Rrc\n", rc));

    PSHCLEVENTPAYLOAD pPayload;
    int rc2 = SharedClipboardPayloadAlloc(pReq->uEvent, pv, cb, &pPayload);
    if (RT_SUCCESS(rc2))
        rc2 = SharedClipboardEventSignal(&pCtx->pClient->Events, pReq->uEvent, pPayload);

    AssertRC(rc);

    RTMemFree(pReq);
}

/**
 * Reads clipboard data from the guest and passes it to the X11 clipboard.
 *
 * @param  pCtx      Pointer to the host clipboard structure
 * @param  u32Format The format in which the data should be transferred
 * @param  ppv       On success and if pcb > 0, this will point to a buffer
 *                   to be freed with RTMemFree containing the data read.
 * @param  pcb       On success, this contains the number of bytes of data
 *                   returned
 * @note   Host glue code.
 */
DECLCALLBACK(int) ClipRequestDataForX11Callback(SHCLCONTEXT *pCtx, uint32_t u32Format, void **ppv, uint32_t *pcb)
{
    LogFlowFunc(("pCtx=%p, u32Format=%02X, ppv=%p\n", pCtx, u32Format, ppv));

    if (pCtx->fShuttingDown)
    {
        /* The shared clipboard is disconnecting. */
        LogRel(("Shared Clipboard: Host requested guest clipboard data after guest had disconnected\n"));
        return VERR_WRONG_ORDER;
    }

    /* Request data from the guest. */
    SHCLDATAREQ dataReq;
    RT_ZERO(dataReq);

    dataReq.uFmt   = u32Format;
    dataReq.cbSize = _64K; /** @todo Make this more dynamic. */

    SHCLEVENTID uEvent;
    int rc = shclSvcDataReadRequest(pCtx->pClient, &dataReq, &uEvent);
    if (RT_SUCCESS(rc))
    {
        PSHCLEVENTPAYLOAD pPayload;
        rc = SharedClipboardEventWait(&pCtx->pClient->Events, uEvent, 30 * 1000, &pPayload);
        if (RT_SUCCESS(rc))
        {
            *ppv = pPayload->pvData;
            *pcb = pPayload->cbData;

            /* Detach the payload, as the caller then will own the data. */
            SharedClipboardEventPayloadDetach(&pCtx->pClient->Events, uEvent);
        }

        SharedClipboardEventUnregister(&pCtx->pClient->Events, uEvent);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
int ShClSvcImplTransferCreate(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer)
{
    RT_NOREF(pClient, pTransfer);
    return VINF_SUCCESS;
}

int ShClSvcImplTransferDestroy(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer)
{
    RT_NOREF(pClient, pTransfer);
    return VINF_SUCCESS;
}
#endif
