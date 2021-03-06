/* $Id$ */
/** @file
 * Virtio_1_0 - Virtio Common (PCI, feature & config mgt, queue mgt & proxy, notification mgt)
 */

/*
 * Copyright (C) 2009-2019 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_DEV_VIRTIO

#include <VBox/log.h>
#include <VBox/msi.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/sg.h>
#include <VBox/vmm/pdmdev.h>
#include "Virtio_1_0_impl.h"
#include "Virtio_1_0.h"

#define INSTANCE(pVirtio) pVirtio->szInstance
#define QUEUENAME(qIdx) (pVirtio->virtqState[qIdx].szVirtqName)


/**
 * See API comments in header file for description
 */
int virtioQueueAttach(VIRTIOHANDLE hVirtio, uint16_t qIdx, const char *pcszName)
{
    LogFunc(("%s\n", pcszName));
    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    PVIRTQSTATE  pVirtq  = &(pVirtio->virtqState[qIdx]);
    pVirtq->uAvailIdx = 0;
    pVirtq->uUsedIdx  = 0;
    pVirtq->fEventThresholdReached = false;
    RTStrCopy((char *)pVirtq->szVirtqName, sizeof(pVirtq->szVirtqName), pcszName);
    return VINF_SUCCESS;
}

/**
 * See API comments in header file for description
 */
const char *virtioQueueGetName(VIRTIOHANDLE hVirtio, uint16_t qIdx)
{
    return (const char *)((PVIRTIOSTATE)hVirtio)->virtqState[qIdx].szVirtqName;
}

/**
 * See API comments in header file for description
 */
int virtioQueueSkip(VIRTIOHANDLE hVirtio, uint16_t qIdx)
{
    Assert(qIdx < sizeof(VIRTQSTATE));

    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    PVIRTQSTATE  pVirtq  = &pVirtio->virtqState[qIdx];

    AssertMsgReturn(DRIVER_OK(pVirtio) && pVirtio->uQueueEnable[qIdx],
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    if (virtioQueueIsEmpty(pVirtio, qIdx))
        return VERR_NOT_AVAILABLE;

    Log2Func(("%s avail_idx=%u\n", pVirtq->szVirtqName, pVirtq->uAvailIdx));
    pVirtq->uAvailIdx++;

    return VINF_SUCCESS;
}

/**
 * See API comments in header file for description
 */
uint64_t virtioGetNegotiatedFeatures(VIRTIOHANDLE hVirtio)
{
    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    return pVirtio->uDriverFeatures;
}

/**
 * See API comments in header file for description
 */
bool virtioQueueIsEmpty(VIRTIOHANDLE hVirtio, uint16_t qIdx)
{
    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    if (!(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK))
        return true;
    return virtqIsEmpty(pVirtio, qIdx);
}

/**
 * See API comments in header file for description
 */
int virtioQueueGet(VIRTIOHANDLE hVirtio, uint16_t qIdx, PPVIRTIO_DESC_CHAIN_T ppDescChain,  bool fRemove)
{
    Assert(ppDescChain);

    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    PVIRTQSTATE  pVirtq  = &pVirtio->virtqState[qIdx];

    PRTSGSEG paSegsIn = (PRTSGSEG)RTMemAlloc(VIRTQ_MAX_SIZE * sizeof(RTSGSEG));
    AssertReturn(paSegsIn, VERR_NO_MEMORY);

    PRTSGSEG paSegsOut = (PRTSGSEG)RTMemAlloc(VIRTQ_MAX_SIZE * sizeof(RTSGSEG));
    AssertReturn(paSegsOut, VERR_NO_MEMORY);

    AssertMsgReturn(DRIVER_OK(pVirtio) && pVirtio->uQueueEnable[qIdx],
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    if (virtqIsEmpty(pVirtio, qIdx))
        return VERR_NOT_AVAILABLE;

    uint16_t uHeadIdx = virtioReadAvailDescIdx(pVirtio, qIdx, pVirtq->uAvailIdx);
    uint16_t uDescIdx = uHeadIdx;

    Log3Func(("%s DESC CHAIN: (head) desc_idx=%u [avail_idx=%u]\n",
            pVirtq->szVirtqName, uHeadIdx, pVirtq->uAvailIdx));

    if (fRemove)
        pVirtq->uAvailIdx++;

    VIRTQ_DESC_T desc;

    uint32_t cbIn = 0, cbOut = 0, cSegsIn = 0, cSegsOut = 0;

    do
    {
        RTSGSEG *pSeg;

        /**
        * Malicious guests may go beyond paSegsIn or paSegsOut boundaries by linking
        * several descriptors into a loop. Since there is no legitimate way to get a sequences of
        * linked descriptors exceeding the total number of descriptors in the ring (see @bugref{8620}),
        * the following aborts I/O if breach and employs a simple log throttling algorithm to notify.
        */
        if (cSegsIn + cSegsOut >= VIRTQ_MAX_SIZE)
        {
            static volatile uint32_t s_cMessages  = 0;
            static volatile uint32_t s_cThreshold = 1;
            if (ASMAtomicIncU32(&s_cMessages) == ASMAtomicReadU32(&s_cThreshold))
            {
                LogRel(("Too many linked descriptors; "
                        "check if the guest arranges descriptors in a loop.\n"));
                if (ASMAtomicReadU32(&s_cMessages) != 1)
                    LogRel(("(the above error has occured %u times so far)\n",
                            ASMAtomicReadU32(&s_cMessages)));
                ASMAtomicWriteU32(&s_cThreshold, ASMAtomicReadU32(&s_cThreshold) * 10);
            }
            break;
        }
        RT_UNTRUSTED_VALIDATED_FENCE();

        virtioReadDesc(pVirtio, qIdx, uDescIdx, &desc);

        if (desc.fFlags & VIRTQ_DESC_F_WRITE)
        {
            Log3Func(("%s IN  desc_idx=%u seg=%u addr=%RGp cb=%u\n",
                QUEUENAME(qIdx), uDescIdx, cSegsIn, desc.pGcPhysBuf, desc.cb));
            cbIn += desc.cb;
            pSeg = &(paSegsIn[cSegsIn++]);
        }
        else
        {
            Log3Func(("%s OUT desc_idx=%u seg=%u addr=%RGp cb=%u\n",
                QUEUENAME(qIdx), uDescIdx, cSegsOut, desc.pGcPhysBuf, desc.cb));
            cbOut += desc.cb;
            pSeg = &(paSegsOut[cSegsOut++]);
        }

        pSeg->pvSeg = (void *)desc.pGcPhysBuf;
        pSeg->cbSeg = desc.cb;

        uDescIdx = desc.uDescIdxNext;
    } while (desc.fFlags & VIRTQ_DESC_F_NEXT);


    PRTSGBUF pSgPhysIn = (PRTSGBUF)RTMemAllocZ(sizeof(RTSGBUF));
    AssertReturn(pSgPhysIn, VERR_NO_MEMORY);

    RTSgBufInit(pSgPhysIn, (PCRTSGSEG)paSegsIn, cSegsIn);

    void *pVirtOut = RTMemAlloc(cbOut);
    AssertReturn(pVirtOut, VERR_NO_MEMORY);

    /* If there's any guest → device data in phys. memory pulled
     * from queue, copy it into virtual memory to return to caller */

    if (cSegsOut)
    {
        uint8_t *outSgVirt = (uint8_t *)pVirtOut;
        RTSGBUF outSgPhys;
        RTSgBufInit(&outSgPhys, (PCRTSGSEG)paSegsOut, cSegsOut);
        for (size_t cb = cbOut; cb;)
        {
            size_t cbSeg = cb;
            RTGCPHYS GCPhys = (RTGCPHYS)RTSgBufGetNextSegment(&outSgPhys, &cbSeg);
            PDMDevHlpPhysRead(((PVIRTIOSTATE)hVirtio)->CTX_SUFF(pDevIns), GCPhys, outSgVirt, cbSeg);
            outSgVirt = ((uint8_t *)outSgVirt) + cbSeg;
            cb -= cbSeg;
        }
        RTMemFree(paSegsOut);
    }

    PVIRTIO_DESC_CHAIN_T pDescChain = (PVIRTIO_DESC_CHAIN_T)RTMemAllocZ(sizeof(VIRTIO_DESC_CHAIN_T));
    AssertReturn(pDescChain, VERR_NO_MEMORY);

    pDescChain->uHeadIdx   = uHeadIdx;
    pDescChain->cbVirtSrc  = cbOut;
    pDescChain->pVirtSrc   = pVirtOut;
    pDescChain->cbPhysDst  = cbIn;
    pDescChain->pSgPhysDst = pSgPhysIn;
    *ppDescChain = pDescChain;

    Log3Func(("%s -- segs OUT: %u (%u bytes)   IN: %u (%u bytes) --\n",
              pVirtq->szVirtqName, cSegsOut, cbOut, cSegsIn, cbIn));

    return VINF_SUCCESS;
}

 /** See API comments in header file prototype for description */
int virtioQueuePut(VIRTIOHANDLE hVirtio, uint16_t qIdx, PRTSGBUF pSgVirtReturn,
                   PVIRTIO_DESC_CHAIN_T pDescChain, bool fFence)
{
    Assert(qIdx < VIRTQ_MAX_CNT);

    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    PVIRTQSTATE  pVirtq = &pVirtio->virtqState[qIdx];
    PRTSGBUF pSgPhysReturn = pDescChain->pSgPhysDst;

    AssertMsgReturn(DRIVER_OK(pVirtio) /*&& pVirtio->uQueueEnable[qIdx]*/,
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    uint16_t uUsedIdx = virtioReadUsedRingIdx(pVirtio, qIdx);
    Log3Func(("Copying client data to %s, desc chain (head desc_idx %d)\n",
               QUEUENAME(qIdx), uUsedIdx));
    (void)uUsedIdx;

    /*
     * Copy virtual memory s/g buffer containing data to return to the guest
     * to phys. memory described by (IN direction ) s/g buffer of the descriptor chain
     * (pulled from avail ring of queue), essentially giving result back to the guest driver.
     */
    size_t cbCopy = 0;
    size_t cbRemain = RTSgBufCalcTotalLength(pSgVirtReturn);
    while (cbRemain)
    {
        PCRTSGSEG paSeg = &pSgPhysReturn->paSegs[pSgPhysReturn->idxSeg];
        uint64_t dstSgStart = (uint64_t)paSeg->pvSeg;
        uint64_t dstSgLen   = (uint64_t)paSeg->cbSeg;
        uint64_t dstSgCur   = (uint64_t)pSgPhysReturn->pvSegCur;
        cbCopy = RT_MIN((uint64_t)pSgVirtReturn->cbSegLeft, dstSgLen - (dstSgCur - dstSgStart));
        PDMDevHlpPhysWrite(pVirtio->CTX_SUFF(pDevIns),
                          (RTGCPHYS)pSgPhysReturn->pvSegCur, pSgVirtReturn->pvSegCur, cbCopy);
        RTSgBufAdvance(pSgVirtReturn, cbCopy);
        RTSgBufAdvance(pSgPhysReturn, cbCopy);
        cbRemain -= cbCopy;
    }

    if (fFence)
        RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    /** If this write-ahead crosses threshold where the driver wants to get an event flag it */
    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
        if (pVirtq->uUsedIdx == virtioReadAvailUsedEvent(pVirtio, qIdx))
            pVirtq->fEventThresholdReached = true;

    Assert(!(cbCopy & 0xffffffff00000000));

    /**
     * Place used buffer's descriptor in used ring but don't update used ring's slot index.
     * That will be done with a subsequent client call to virtioQueueSync() */
    virtioWriteUsedElem(pVirtio, qIdx, pVirtq->uUsedIdx++, pDescChain->uHeadIdx,
            (uint32_t)(cbCopy & 0xffffffff));

    Log2Func((".... Copied %ul bytes to %ul byte buffer, residual=%ul\n",
         cbCopy, pDescChain->cbPhysDst, pDescChain->cbPhysDst - cbCopy));

    Log6Func(("Write ahead used_idx=%d, %s used_idx=%d\n",
         pVirtq->uUsedIdx,  QUEUENAME(qIdx), uUsedIdx));

    RTMemFree((void *)pSgPhysReturn->paSegs);
    RTMemFree(pSgPhysReturn);
    RTMemFree(pDescChain);

    return VINF_SUCCESS;
}

/**
 * See API comments in header file for description
 */
int virtioQueueSync(VIRTIOHANDLE hVirtio, uint16_t qIdx)
{
    Assert(qIdx < sizeof(VIRTQSTATE));

    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[qIdx];

    AssertMsgReturn(DRIVER_OK(pVirtio) && pVirtio->uQueueEnable[qIdx],
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    uint16_t uIdx = virtioReadUsedRingIdx(pVirtio, qIdx);
    Log6Func(("Updating %s used_idx from %u to %u\n",
              QUEUENAME(qIdx), uIdx, pVirtq->uUsedIdx));
    (void)uIdx;

    virtioWriteUsedRingIdx(pVirtio, qIdx, pVirtq->uUsedIdx);
    virtioNotifyGuestDriver(pVirtio, qIdx, false);

    return VINF_SUCCESS;
}

/**
 * See API comments in header file for description
 */
static void virtioQueueNotified(PVIRTIOSTATE pVirtio, uint16_t qIdx, uint16_t uNotifyIdx)
{
    Assert(uNotifyIdx == qIdx);
    (void)uNotifyIdx;

    PVIRTQSTATE pVirtq = &pVirtio->virtqState[qIdx];
    Log6Func(("%s\n", pVirtq->szVirtqName));
    (void)pVirtq;

    /** Inform client */
    pVirtio->virtioCallbacks.pfnVirtioQueueNotified((VIRTIOHANDLE)pVirtio, pVirtio->pClientContext, qIdx);
}

/**
 * See API comments in header file for description
 */
void virtioPropagateResumeNotification(VIRTIOHANDLE hVirtio)
{
    virtioNotifyGuestDriver((PVIRTIOSTATE)hVirtio, (uint16_t)NULL /* qIdx */, true /* fForce */);
}

/**
 * Trigger MSI-X or INT# interrupt to notify guest of data added to used ring of
 * the specified virtq, depending on the interrupt configuration of the device
 * and depending on negotiated and realtime constraints flagged by the guest driver.
 * See VirtIO 1.0 specification (section 2.4.7).
 *
 * @param pVirtio       - Instance state
 * @param qIdx          - Queue to check for guest interrupt handling preference
 * @param fForce        - Overrides qIdx, forcing notification regardless of driver's
 *                        notification preferences. This is a safeguard to prevent
 *                        stalls upon resuming the VM. VirtIO 1.0 specification Section 4.1.5.5
 *                        indicates spurious interrupts are harmless to guest driver's state,
 *                        as they only cause the guest driver to [re]scan queues for work to do.
 */
static void virtioNotifyGuestDriver(PVIRTIOSTATE pVirtio, uint16_t qIdx, bool fForce)
{
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[qIdx];

    AssertMsgReturnVoid(DRIVER_OK(pVirtio), ("Guest driver not in ready state.\n"));
    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
    {
        if (pVirtq->fEventThresholdReached)
        {
            virtioKick(pVirtio, VIRTIO_ISR_VIRTQ_INTERRUPT, pVirtio->uQueueMsixVector[qIdx], fForce);
            pVirtq->fEventThresholdReached = false;
            return;
        }
        Log6Func(("...skipping interrupt: VIRTIO_F_EVENT_IDX set but threshold not reached\n"));
    }
    else
    {
        /** If guest driver hasn't suppressed interrupts, interrupt  */
        if (fForce || !(virtioReadUsedFlags(pVirtio, qIdx) & VIRTQ_AVAIL_F_NO_INTERRUPT))
        {
            virtioKick(pVirtio, VIRTIO_ISR_VIRTQ_INTERRUPT, pVirtio->uQueueMsixVector[qIdx], fForce);
            return;
        }
        Log6Func(("...skipping interrupt. Guest flagged VIRTQ_AVAIL_F_NO_INTERRUPT for queue\n"));
    }
}

/**
 * NOTE: The consumer (PDM device) must call this function to 'forward' a relocation call.
 *
 * Device relocation callback.
 *
 * When this callback is called the device instance data, and if the
 * device have a GC component, is being relocated, or/and the selectors
 * have been changed. The device must use the chance to perform the
 * necessary pointer relocations and data updates.
 *
 * Before the GC code is executed the first time, this function will be
 * called with a 0 delta so GC pointer calculations can be one in one place.
 *
 * @param   pDevIns     Pointer to the device instance.
 * @param   offDelta    The relocation delta relative to the old location.
 *
 * @remark  A relocation CANNOT fail.
 */
void virtioRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    RT_NOREF(offDelta);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    LogFunc(("\n"));

    pVirtio->pDevInsR3 = pDevIns;
    pVirtio->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    pVirtio->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
}


/**
 * Raise interrupt or MSI-X
 *
 * @param   pVirtio         The device state structure.
 * @param   uCause          Interrupt cause bit mask to set in PCI ISR port.
 * @param   uVec            MSI-X vector, if enabled
 * @param   uForce          True of out-of-band
 */
static int virtioKick(PVIRTIOSTATE pVirtio, uint8_t uCause, uint16_t uMsixVector, bool fForce)
{

   if (fForce)
       Log6Func(("reason: resumed after suspend\n"));
   else
   if (uCause == VIRTIO_ISR_VIRTQ_INTERRUPT)
       Log6Func(("reason: buffer added to 'used' ring.\n"));
   else
   if (uCause == VIRTIO_ISR_DEVICE_CONFIG)
       Log6Func(("reason: device config change\n"));

    if (!pVirtio->fMsiSupport)
    {
        pVirtio->uISR |= uCause;
        PDMDevHlpPCISetIrq(pVirtio->CTX_SUFF(pDevIns), 0, PDM_IRQ_LEVEL_HIGH);
    }
    else if (uMsixVector != VIRTIO_MSI_NO_VECTOR)
    {
        Log6Func(("MSI-X enabled, calling PDMDevHlpPCISetIrq with vector: 0x%x\n", uMsixVector));
        PDMDevHlpPCISetIrq(pVirtio->CTX_SUFF(pDevIns), uMsixVector, 1);
    }
    return VINF_SUCCESS;
}

/**
 * Lower interrupt. (Called when guest reads ISR)
 *
 * @param   pVirtio      The device state structure.
 */
static void virtioLowerInterrupt(PVIRTIOSTATE pVirtio)
{
    PDMDevHlpPCISetIrq(pVirtio->CTX_SUFF(pDevIns), 0, PDM_IRQ_LEVEL_LOW);
}

static void virtioResetQueue(PVIRTIOSTATE pVirtio, uint16_t qIdx)
{
    PVIRTQSTATE pVirtQ = &pVirtio->virtqState[qIdx];
    pVirtQ->uAvailIdx = 0;
    pVirtQ->uUsedIdx  = 0;
    pVirtio->uQueueEnable[qIdx] = false;
    pVirtio->uQueueSize[qIdx] = VIRTQ_MAX_SIZE;
    pVirtio->uQueueNotifyOff[qIdx] = qIdx;

    pVirtio->uQueueMsixVector[qIdx] = qIdx + 2;
    if (!pVirtio->fMsiSupport) /* VirtIO 1.0, 4.1.4.3 and 4.1.5.1.2 */
        pVirtio->uQueueMsixVector[qIdx] = VIRTIO_MSI_NO_VECTOR;
}

static void virtioResetDevice(PVIRTIOSTATE pVirtio)
{
    Log2Func(("\n"));
    pVirtio->uDeviceFeaturesSelect  = 0;
    pVirtio->uDriverFeaturesSelect  = 0;
    pVirtio->uConfigGeneration      = 0;
    pVirtio->uDeviceStatus          = 0;
    pVirtio->uISR                   = 0;


    if (!pVirtio->fMsiSupport)  /* VirtIO 1.0, 4.1.4.3 and 4.1.5.1.2 */
        pVirtio->uMsixConfig = VIRTIO_MSI_NO_VECTOR;

    pVirtio->uNumQueues = VIRTQ_MAX_CNT;
    for (uint16_t qIdx = 0; qIdx < pVirtio->uNumQueues; qIdx++)
        virtioResetQueue(pVirtio, qIdx);
}

/**
 * See API comments in header file for description
 */
bool virtioIsQueueEnabled(VIRTIOHANDLE hVirtio, uint16_t qIdx)
{
    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    return pVirtio->uQueueEnable[qIdx] != 0;
}

/**
 * See API comments in header file for description
 */
void virtioQueueEnable(VIRTIOHANDLE hVirtio, uint16_t qIdx, bool fEnabled)
{
    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    if (fEnabled)
        pVirtio->uQueueSize[qIdx] = VIRTQ_MAX_SIZE;
    else
        pVirtio->uQueueSize[qIdx] = 0;
}

/**
 * Initiate orderly reset procedure.
 * Invoked by client to reset the device and driver (see VirtIO 1.0 section 2.1.1/2.1.2)
 */
void virtioResetAll(VIRTIOHANDLE hVirtio)
{
    LogFunc(("VIRTIO RESET REQUESTED!!!\n"));
    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)hVirtio;
    pVirtio->uDeviceStatus |= VIRTIO_STATUS_DEVICE_NEEDS_RESET;
    if (pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK)
    {
        pVirtio->fGenUpdatePending = true;
        virtioKick(pVirtio, VIRTIO_ISR_DEVICE_CONFIG, pVirtio->uMsixConfig, false /* fForce */);
    }
}

/**
 * Invoked by this implementation when guest driver resets the device.
 * The driver itself will not  until the device has read the status change.
 */
static void virtioGuestResetted(PVIRTIOSTATE pVirtio)
{
    LogFunc(("Guest reset the device\n"));

    /** Let the client know */
    pVirtio->virtioCallbacks.pfnVirtioStatusChanged((VIRTIOHANDLE)pVirtio, pVirtio->pClientContext, 0);
    virtioResetDevice(pVirtio);
}

/**
 * Handle accesses to Common Configuration capability
 *
 * @returns VBox status code
 *
 * @param   pVirtio     Virtio instance state
 * @param   fWrite      Set if write access, clear if read access.
 * @param   pv          Pointer to location to write to or read from
 * @param   cb          Number of bytes to read or write
 */
static int virtioCommonCfgAccessed(PVIRTIOSTATE pVirtio, int fWrite, off_t uOffset, unsigned cb, void const *pv)
{
    int rc = VINF_SUCCESS;
    uint64_t val;
    if (MATCH_COMMON_CFG(uDeviceFeatures))
    {
        if (fWrite) /* Guest WRITE pCommonCfg>uDeviceFeatures */
            LogFunc(("Guest attempted to write readonly virtio_pci_common_cfg.device_feature\n"));
        else /* Guest READ pCommonCfg->uDeviceFeatures */
        {
            uint32_t uIntraOff = uOffset - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDeviceFeatures);
            switch(pVirtio->uDeviceFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDeviceFeatures & 0xffffffff;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_COMMON_CFG_ACCESS(uDeviceFeatures);
                    break;
                case 1:
                    val = (pVirtio->uDeviceFeatures >> 32) & 0xffffffff;
                    uIntraOff += 4;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_COMMON_CFG_ACCESS(uDeviceFeatures);
                    break;
                default:
                    LogFunc(("Guest read uDeviceFeatures with out of range selector (%d), returning 0\n",
                          pVirtio->uDeviceFeaturesSelect));
                    return VERR_ACCESS_DENIED;
            }
        }
    }
    else if (MATCH_COMMON_CFG(uDriverFeatures))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->udriverFeatures */
        {
            uint32_t uIntraOff = uOffset - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures);
            switch(pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    memcpy(&pVirtio->uDriverFeatures, pv, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures);
                    break;
                case 1:
                    memcpy(((char *)&pVirtio->uDriverFeatures) + sizeof(uint32_t), pv, cb);
                    uIntraOff += 4;
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures);
                    break;
                default:
                    LogFunc(("Guest wrote uDriverFeatures with out of range selector (%d), returning 0\n",
                         pVirtio->uDriverFeaturesSelect));
                    return VERR_ACCESS_DENIED;
            }
        }
        else /* Guest READ pCommonCfg->udriverFeatures */
        {
            uint32_t uIntraOff = uOffset - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures);
            switch(pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDriverFeatures & 0xffffffff;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures);
                    break;
                case 1:
                    val = (pVirtio->uDriverFeatures >> 32) & 0xffffffff;
                    uIntraOff += 4;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures);
                    break;
                default:
                    LogFunc(("Guest read uDriverFeatures with out of range selector (%d), returning 0\n",
                         pVirtio->uDriverFeaturesSelect));
                    return VERR_ACCESS_DENIED;
            }
        }
    }
    else if (MATCH_COMMON_CFG(uNumQueues))
    {
        if (fWrite)
        {
            Log2Func(("Guest attempted to write readonly virtio_pci_common_cfg.num_queues\n"));
            return VERR_ACCESS_DENIED;
        }
        else
        {
            uint32_t uIntraOff = 0;
            *(uint16_t *)pv = VIRTQ_MAX_CNT;
            LOG_COMMON_CFG_ACCESS(uNumQueues);
        }
    }
    else if (MATCH_COMMON_CFG(uDeviceStatus))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->uDeviceStatus */
        {
            pVirtio->uDeviceStatus = *(uint8_t *)pv;
            Log6Func(("Guest wrote uDeviceStatus ................ ("));
            virtioLogDeviceStatus(pVirtio->uDeviceStatus);
            Log6((")\n"));
            if (pVirtio->uDeviceStatus == 0)
                virtioGuestResetted(pVirtio);
            /**
             * Notify client only if status actually changed from last time.
             */
            uint32_t fOkayNow = pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK;
            uint32_t fWasOkay = pVirtio->uPrevDeviceStatus & VIRTIO_STATUS_DRIVER_OK;
            uint32_t fDrvOk = pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK;
            if ((fOkayNow && !fWasOkay) || (!fOkayNow && fWasOkay))
                pVirtio->virtioCallbacks.pfnVirtioStatusChanged((VIRTIOHANDLE)pVirtio, pVirtio->pClientContext, fDrvOk);
            pVirtio->uPrevDeviceStatus = pVirtio->uDeviceStatus;
        }
        else /* Guest READ pCommonCfg->uDeviceStatus */
        {
            Log6Func(("Guest read  uDeviceStatus ................ ("));
            *(uint32_t *)pv = pVirtio->uDeviceStatus;
            virtioLogDeviceStatus(pVirtio->uDeviceStatus);
            Log6((")\n"));
        }
    }
    else
    if (MATCH_COMMON_CFG(uMsixConfig))
        COMMON_CFG_ACCESSOR(uMsixConfig);
    else
    if (MATCH_COMMON_CFG(uDeviceFeaturesSelect))
        COMMON_CFG_ACCESSOR(uDeviceFeaturesSelect);
    else
    if (MATCH_COMMON_CFG(uDriverFeaturesSelect))
        COMMON_CFG_ACCESSOR(uDriverFeaturesSelect);
    else
    if (MATCH_COMMON_CFG(uConfigGeneration))
        COMMON_CFG_ACCESSOR_READONLY(uConfigGeneration);
    else
    if (MATCH_COMMON_CFG(uQueueSelect))
        COMMON_CFG_ACCESSOR(uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueSize))
        COMMON_CFG_ACCESSOR_INDEXED(uQueueSize, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueMsixVector))
        COMMON_CFG_ACCESSOR_INDEXED(uQueueMsixVector, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueEnable))
        COMMON_CFG_ACCESSOR_INDEXED(uQueueEnable, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueNotifyOff))
        COMMON_CFG_ACCESSOR_INDEXED_READONLY(uQueueNotifyOff, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(pGcPhysQueueDesc))
        COMMON_CFG_ACCESSOR_INDEXED(pGcPhysQueueDesc, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(pGcPhysQueueAvail))
        COMMON_CFG_ACCESSOR_INDEXED(pGcPhysQueueAvail, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(pGcPhysQueueUsed))
        COMMON_CFG_ACCESSOR_INDEXED(pGcPhysQueueUsed, pVirtio->uQueueSelect);
    else
    {
        Log2Func(("Bad guest %s access to virtio_pci_common_cfg: uOffset=%d, cb=%d\n",
            fWrite ? "write" : "read ", uOffset, cb));
        rc = VERR_ACCESS_DENIED;
    }
    return rc;
}

/**
 * Memory mapped I/O Handler for PCI Capabilities read operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   GCPhysAddr  Physical address (in GC) where the read starts.
 * @param   pv          Where to store the result.
 * @param   cb          Number of bytes read.
 */
PDMBOTHCBDECL(int) virtioR3MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    RT_NOREF(pvUser);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    int rc = VINF_SUCCESS;

    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysDeviceCap, pVirtio->pDeviceCap,     fDevSpecific);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysCommonCfg, pVirtio->pCommonCfgCap,  fCommonCfg);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysIsrCap,    pVirtio->pIsrCap,        fIsr);

    if (fDevSpecific)
    {
        uint32_t uOffset = GCPhysAddr - pVirtio->pGcPhysDeviceCap;
        /*
         * Callback to client to manage device-specific configuration.
         */
        rc = pVirtio->virtioCallbacks.pfnVirtioDevCapRead(pDevIns, uOffset, pv, cb);

        /*
         * Additionally, anytime any part of the device-specific configuration (which our client maintains)
         * is READ it needs to be checked to see if it changed since the last time any part was read, in
         * order to maintain the config generation (see VirtIO 1.0 spec, section 4.1.4.3.1)
         */
        bool fDevSpecificFieldChanged = !!memcmp((char *)pVirtio->pDevSpecificCfg + uOffset,
                   (char *)pVirtio->pPrevDevSpecificCfg + uOffset, cb);

        memcpy(pVirtio->pPrevDevSpecificCfg, pVirtio->pDevSpecificCfg, pVirtio->cbDevSpecificCfg);

        if (pVirtio->fGenUpdatePending || fDevSpecificFieldChanged)
        {
            ++pVirtio->uConfigGeneration;
            Log6Func(("Bumped cfg. generation to %d because %s%s\n",
                pVirtio->uConfigGeneration,
                fDevSpecificFieldChanged ? "<dev cfg changed> " : "",
                pVirtio->fGenUpdatePending ? "<update was pending>" : ""));
            pVirtio->fGenUpdatePending = false;
        }
    }
    else
    if (fCommonCfg)
    {
        uint32_t uOffset = GCPhysAddr - pVirtio->pGcPhysCommonCfg;
        virtioCommonCfgAccessed(pVirtio, false /* fWrite */, uOffset, cb, (void const *)pv);
    }
    else
    if (fIsr && cb == sizeof(uint8_t))
    {
        *(uint8_t *)pv = pVirtio->uISR;
        Log6Func(("Read and clear ISR\n"));
        pVirtio->uISR = 0; /* VirtIO specification requires reads of ISR to clear it */
        virtioLowerInterrupt(pVirtio);
    }
    else {
       LogFunc(("Bad read access to mapped capabilities region:\n"
                "                  pVirtio=%#p GCPhysAddr=%RGp cb=%u\n",
                pVirtio, GCPhysAddr, cb));
    }
    return rc;
}
/**
 * Memory mapped I/O Handler for PCI Capabilities write operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   GCPhysAddr  Physical address (in GC) where the write starts.
 * @param   pv          Where to fetch the result.
 * @param   cb          Number of bytes to write.
 */
PDMBOTHCBDECL(int) virtioR3MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void const *pv, unsigned cb)
{
    RT_NOREF(pvUser);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    int rc = VINF_SUCCESS;

    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysDeviceCap, pVirtio->pDeviceCap,     fDevSpecific);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysCommonCfg, pVirtio->pCommonCfgCap,  fCommonCfg);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysIsrCap,    pVirtio->pIsrCap,        fIsr);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysNotifyCap, pVirtio->pNotifyCap,     fNotify);

    if (fDevSpecific)
    {
        uint32_t uOffset = GCPhysAddr - pVirtio->pGcPhysDeviceCap;
        /*
         * Pass this MMIO write access back to the client to handle
         */
        rc = pVirtio->virtioCallbacks.pfnVirtioDevCapWrite(pDevIns, uOffset, pv, cb);
    }
    else
    if (fCommonCfg)
    {
        uint32_t uOffset = GCPhysAddr - pVirtio->pGcPhysCommonCfg;
        virtioCommonCfgAccessed(pVirtio, true /* fWrite */, uOffset, cb, pv);
    }
    else
    if (fIsr && cb == sizeof(uint8_t))
    {
        pVirtio->uISR = *(uint8_t *)pv;
        Log6Func(("Setting uISR = 0x%02x (virtq interrupt: %d, dev confg interrupt: %d)\n",
              pVirtio->uISR & 0xff,
              pVirtio->uISR & VIRTIO_ISR_VIRTQ_INTERRUPT,
              !!(pVirtio->uISR & VIRTIO_ISR_DEVICE_CONFIG)));
    }
    else
    /* This *should* be guest driver dropping index of a new descriptor in avail ring */
    if (fNotify && cb == sizeof(uint16_t))
    {
        uint32_t uNotifyBaseOffset = GCPhysAddr - pVirtio->pGcPhysNotifyCap;
        uint16_t qIdx = uNotifyBaseOffset / VIRTIO_NOTIFY_OFFSET_MULTIPLIER;
        uint16_t uAvailDescIdx = *(uint16_t *)pv;
        virtioQueueNotified(pVirtio, qIdx, uAvailDescIdx);
    }
    else
    {
       Log2Func(("Bad write access to mapped capabilities region:\n"
                "                  pVirtio=%#p GCPhysAddr=%RGp pv=%#p{%.*Rhxs} cb=%u\n",
                pVirtio, GCPhysAddr, pv, cb, pv, cb));
    }
    return rc;
}

/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
static DECLCALLBACK(int) virtioR3Map(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                     RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    int          rc      = VINF_SUCCESS;
    RT_NOREF3(pPciDev, iRegion, enmType);
    Assert(pPciDev == pDevIns->apPciDevs[0]);

    Assert(cb >= 32);

    if (iRegion == VIRTIO_REGION_PCI_CAP)
    {
        /* We use the assigned size here, because we currently only support page aligned MMIO ranges. */
        rc = PDMDevHlpMMIORegister(pDevIns, GCPhysAddress, cb, NULL /*pvUser*/,
                           IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                           virtioR3MmioWrite, virtioR3MmioRead,
                           "virtio-scsi MMIO");

        if (RT_FAILURE(rc))
        {
            Log2Func(("virtio: PCI Capabilities failed to map GCPhysAddr=%RGp cb=%RGp, region=%d\n",
                    GCPhysAddress, cb, iRegion));
            return rc;
        }
        Log2Func(("virtio: PCI Capabilities mapped at GCPhysAddr=%RGp cb=%RGp, region=%d\n",
                GCPhysAddress, cb, iRegion));
        pVirtio->pGcPhysPciCapBase = GCPhysAddress;
        pVirtio->pGcPhysCommonCfg  = GCPhysAddress + pVirtio->pCommonCfgCap->uOffset;
        pVirtio->pGcPhysNotifyCap  = GCPhysAddress + pVirtio->pNotifyCap->pciCap.uOffset;
        pVirtio->pGcPhysIsrCap     = GCPhysAddress + pVirtio->pIsrCap->uOffset;
        if (pVirtio->pPrevDevSpecificCfg)
            pVirtio->pGcPhysDeviceCap = GCPhysAddress + pVirtio->pDeviceCap->uOffset;
    }
    return rc;
}

/**
 * @callback_method_impl{FNPCICONFIGRead}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioR3PciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                        uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    RT_NOREF(pPciDev, cb);

    uint64_t uWindowOff = (uint64_t)uAddress - (uint64_t)pVirtio->pPciCfgCap->uPciCfgData;
    if (uWindowOff < sizeof(uint32_t))
    {
        /* VirtIO 1.0 spec section 4.1.4.7 describes a required alternative access capability
         * whereby the guest driver can specify a bar, offset, and length via the PCI configuration space
         * (the virtio_pci_cfg_cap capability), and access data items. */

        uint32_t uLength = pVirtio->pPciCfgCap->pciCap.uLength;
        uint32_t uOffset = pVirtio->pPciCfgCap->pciCap.uOffset;
        uint8_t  uBar    = pVirtio->pPciCfgCap->pciCap.uBar;

        AssertReturn(uLength == 1 || uLength == 2 || uLength == 4, VERR_INVALID_PARAMETER);
        AssertReturn(cb + uWindowOff <= uLength, VERR_INVALID_PARAMETER);

        *pu32Value = 0xffffffff;
        if (uBar == VIRTIO_REGION_PCI_CAP)
        {
            uint32_t pu32tmp = 0;
            virtioR3MmioRead(pDevIns, NULL, (RTGCPHYS)((uint32_t)pVirtio->pGcPhysPciCapBase + uOffset), &pu32tmp, uLength);
            memcpy(pu32Value + uWindowOff, &pu32tmp + uWindowOff, cb);
            Log2Func(("virtio: Guest read  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%d, length=%d, result=%d\n",
                      uBar, uOffset, uLength, *pu32Value));
        }
        else
            Log2Func(("Guest read virtio_pci_cfg_cap.pci_cfg_data using unconfigured BAR. Ignoring"));
        return VINF_SUCCESS;
    }
    return VINF_PDM_PCI_DO_DEFAULT;
}

/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioR3PciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    RT_NOREF(pPciDev, cb);

    uint64_t uWindowOff = (uint64_t)uAddress - (uint64_t)pVirtio->pPciCfgCap->uPciCfgData;
    if (uWindowOff < sizeof(uint32_t))
    {
        /* VirtIO 1.0 spec section 4.1.4.7 describes a required alternative access capability
         * whereby the guest driver can specify a bar, offset, and length via the PCI configuration space
         * (the virtio_pci_cfg_cap capability), and access data items. */

        uint32_t uLength = pVirtio->pPciCfgCap->pciCap.uLength;
        uint32_t uOffset = pVirtio->pPciCfgCap->pciCap.uOffset;
        uint8_t  uBar    = pVirtio->pPciCfgCap->pciCap.uBar;

        AssertReturn(uLength == 1 || uLength == 2 || uLength == 4, VERR_INVALID_PARAMETER);
        AssertReturn(cb == uLength && uWindowOff == 0, VERR_INVALID_PARAMETER);

        if (uBar == VIRTIO_REGION_PCI_CAP)
        {
            Assert(uLength <= sizeof(u32Value));
            virtioR3MmioWrite(pDevIns, NULL, (RTGCPHYS)((uint32_t)pVirtio->pGcPhysPciCapBase + uOffset), &u32Value, uLength);
        }
        else
        {
            Log2Func(("Guest wrote virtio_pci_cfg_cap.pci_cfg_data using unconfigured BAR. Ignoring"));
            return VINF_SUCCESS;
        }
        Log2Func(("Guest wrote  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%x, length=%x, value=%d\n",
                uBar, uOffset, uLength, u32Value));
        return VINF_SUCCESS;
    }
    return VINF_PDM_PCI_DO_DEFAULT;
}

/**
 * Get VirtIO accepted host-side features
 *
 * @returns feature bits selected or 0 if selector out of range.
 *
 * @param   pState          Virtio state
 */
uint64_t virtioGetAcceptedFeatures(PVIRTIOSTATE pVirtio)
{
    return pVirtio->uDriverFeatures;
}

/**
 * Destruct PCI-related part of device.
 *
 * We need to free non-VM resources only.
 *
 * @returns VBox status code.
 * @param   pState      The device state structure.
 */
int virtioDestruct(PVIRTIOSTATE pVirtio)
{
    RT_NOREF(pVirtio);
    Log(("%s Destroying PCI instance\n", INSTANCE(pVirtio)));
    return VINF_SUCCESS;
}

/**
 * Setup PCI device controller and Virtio state
 *
 * @param   pDevIns                  Device instance data
 * @param   pClientContext           Opaque client context (such as state struct, ...)
 * @param   pVirtio                  Device State
 * @param   pPciParams               Values to populate industry standard PCI Configuration Space data structure
 * @param   pcszInstance             Device instance name (format-specifier)
 * @param   uDevSpecificFeatures     VirtIO device-specific features offered by client
 * @param   devCapReadCallback       Client handler to call upon guest read to device specific capabilities.
 * @param   devCapWriteCallback      Client handler to call upon guest write to device specific capabilities.
 * @param   devStatusChangedCallback Client handler to call for major device status changes
 * @param   queueNotifiedCallback    Client handler for guest-to-host notifications that avail queue has ring data
 * @param   ssmLiveExecCallback      Client handler for SSM live exec
 * @param   ssmSaveExecCallback      Client handler for SSM save exec
 * @param   ssmLoadExecCallback      Client handler for SSM load exec
 * @param   ssmLoadDoneCallback      Client handler for SSM load done
 * @param   cbDevSpecificCfg         Size of virtio_pci_device_cap device-specific struct
 * @param   pDevSpecificCfg          Address of client's dev-specific configuration struct.
 */
int   virtioConstruct(PPDMDEVINS             pDevIns,
                      void                  *pClientContext,
                      VIRTIOHANDLE          *phVirtio,
                      PVIRTIOPCIPARAMS       pPciParams,
                      const char            *pcszInstance,
                      uint64_t               uDevSpecificFeatures,
                      PFNVIRTIODEVCAPREAD    devCapReadCallback,
                      PFNVIRTIODEVCAPWRITE   devCapWriteCallback,
                      PFNVIRTIOSTATUSCHANGED devStatusChangedCallback,
                      PFNVIRTIOQUEUENOTIFIED queueNotifiedCallback,
                      PFNSSMDEVLIVEEXEC      ssmLiveExecCallback,
                      PFNSSMDEVSAVEEXEC      ssmSaveExecCallback,
                      PFNSSMDEVLOADEXEC      ssmLoadExecCallback,
                      PFNSSMDEVLOADDONE      ssmLoadDoneCallback,
                      uint16_t               cbDevSpecificCfg,
                      void                  *pDevSpecificCfg)
{

    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)RTMemAllocZ(sizeof(VIRTIOSTATE));
    if (!pVirtio)
    {
        PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY, N_("virtio: out of memory"));
        return VERR_NO_MEMORY;
    }

#ifdef VBOX_WITH_MSI_DEVICES
    pVirtio->fMsiSupport = true;
#endif

    pVirtio->pClientContext = pClientContext;

    /*
     * The host features offered include both device-specific features
     * and reserved feature bits (device independent)
     */
    pVirtio->uDeviceFeatures =   VIRTIO_F_VERSION_1
                               | VIRTIO_DEV_INDEPENDENT_FEATURES_OFFERED
                               | uDevSpecificFeatures;

    RTStrCopy(pVirtio->szInstance, sizeof(pVirtio->szInstance), pcszInstance);

    pVirtio->pDevInsR3 = pDevIns;
    pVirtio->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
    pVirtio->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    pVirtio->uDeviceStatus = 0;
    pVirtio->cbDevSpecificCfg = cbDevSpecificCfg;
    pVirtio->pDevSpecificCfg  = pDevSpecificCfg;

    pVirtio->pPrevDevSpecificCfg = RTMemAllocZ(cbDevSpecificCfg);
    if (!pVirtio->pPrevDevSpecificCfg)
    {
        RTMemFree(pVirtio);
        PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY, N_("virtio: out of memory"));
        return VERR_NO_MEMORY;
    }

    memcpy(pVirtio->pPrevDevSpecificCfg, pVirtio->pDevSpecificCfg, cbDevSpecificCfg);
    pVirtio->virtioCallbacks.pfnVirtioDevCapRead    = devCapReadCallback;
    pVirtio->virtioCallbacks.pfnVirtioDevCapWrite   = devCapWriteCallback;
    pVirtio->virtioCallbacks.pfnVirtioStatusChanged = devStatusChangedCallback;
    pVirtio->virtioCallbacks.pfnVirtioQueueNotified = queueNotifiedCallback;
    pVirtio->virtioCallbacks.pfnSSMDevLiveExec      = ssmLiveExecCallback;
    pVirtio->virtioCallbacks.pfnSSMDevSaveExec      = ssmSaveExecCallback;
    pVirtio->virtioCallbacks.pfnSSMDevLoadExec      = ssmLoadExecCallback;
    pVirtio->virtioCallbacks.pfnSSMDevLoadDone      = ssmLoadDoneCallback;

    /* Set PCI config registers (assume 32-bit mode) */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetRevisionId(pPciDev,         DEVICE_PCI_REVISION_ID_VIRTIO);
    PDMPciDevSetVendorId(pPciDev,           DEVICE_PCI_VENDOR_ID_VIRTIO);
    PDMPciDevSetSubSystemVendorId(pPciDev,  DEVICE_PCI_VENDOR_ID_VIRTIO);
    PDMPciDevSetDeviceId(pPciDev,           pPciParams->uDeviceId);
    PDMPciDevSetClassBase(pPciDev,          pPciParams->uClassBase);
    PDMPciDevSetClassSub(pPciDev,           pPciParams->uClassSub);
    PDMPciDevSetClassProg(pPciDev,          pPciParams->uClassProg);
    PDMPciDevSetSubSystemId(pPciDev,        pPciParams->uSubsystemId);
    PDMPciDevSetInterruptLine(pPciDev,      pPciParams->uInterruptLine);
    PDMPciDevSetInterruptPin(pPciDev,       pPciParams->uInterruptPin);

    /* Register PCI device */
    int rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pVirtio);
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register PCI Device")); /* can we put params in this error? */
    }

    rc = PDMDevHlpSSMRegisterEx(pDevIns, VIRTIO_SAVEDSTATE_VERSION, sizeof(*pVirtio), NULL,
                                NULL, virtioR3LiveExec, NULL, NULL, virtioR3SaveExec, NULL,
                                NULL, virtioR3LoadExec, virtioR3LoadDone);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pVirtio);
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register SSM callbacks"));
    }

    rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, virtioR3PciConfigRead, virtioR3PciConfigWrite);
    AssertRCReturnStmt(rc, RTMemFree(pVirtio), rc);


    /* Construct & map PCI vendor-specific capabilities for virtio host negotiation with guest driver */

    /* The following capability mapped via VirtIO 1.0: struct virtio_pci_cfg_cap (VIRTIO_PCI_CFG_CAP_T)
     * as a mandatory but suboptimal alternative interface to host device capabilities, facilitating
     * access the memory of any BAR. If the guest uses it (the VirtIO driver on Linux doesn't),
     * Unlike Common, Notify, ISR and Device capabilities, it is accessed directly via PCI Config region.
     * therefore does not contribute to the capabilities region (BAR) the other capabilities use.
     */
#define CFGADDR2IDX(addr) ((uint8_t)(((uintptr_t)(addr) - (uintptr_t)&pPciDev->abConfig[0])))

    PVIRTIO_PCI_CAP_T pCfg;
    uint32_t cbRegion = 0;

    /* Common capability (VirtIO 1.0 spec, section 4.1.4.3) */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[0x40];
    pCfg->uCfgType = VIRTIO_PCI_CAP_COMMON_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFGADDR2IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = RT_ALIGN_32(0, 4); /* reminder, in case someone changes offset */
    pCfg->uLength  = sizeof(VIRTIO_PCI_COMMON_CFG_T);
    cbRegion += pCfg->uLength;
    pVirtio->pCommonCfgCap = pCfg;

    /*
     * Notify capability (VirtIO 1.0 spec, section 4.1.4.4). Note: uLength is based the choice
     * of this implementation that each queue's uQueueNotifyOff is set equal to (QueueSelect) ordinal
     * value of the queue */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_NOTIFY_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_NOTIFY_CAP_T);
    pCfg->uCapNext = CFGADDR2IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = pVirtio->pCommonCfgCap->uOffset + pVirtio->pCommonCfgCap->uLength;
    pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 2);
    pCfg->uLength  = VIRTQ_MAX_CNT * VIRTIO_NOTIFY_OFFSET_MULTIPLIER + 2;  /* will change in VirtIO 1.1 */
    cbRegion += pCfg->uLength;
    pVirtio->pNotifyCap = (PVIRTIO_PCI_NOTIFY_CAP_T)pCfg;
    pVirtio->pNotifyCap->uNotifyOffMultiplier = VIRTIO_NOTIFY_OFFSET_MULTIPLIER;

    /* ISR capability (VirtIO 1.0 spec, section 4.1.4.5)
     *
     * VirtIO 1.0 spec says 8-bit, unaligned in MMIO space. Example/diagram
     * of spec shows it as a 32-bit field with upper bits 'reserved'
     * Will take spec words more literally than the diagram for now.
     */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_ISR_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFGADDR2IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = pVirtio->pNotifyCap->pciCap.uOffset + pVirtio->pNotifyCap->pciCap.uLength;
    pCfg->uLength  = sizeof(uint8_t);
    cbRegion += pCfg->uLength;
    pVirtio->pIsrCap = pCfg;

    /*  PCI Cfg capability (VirtIO 1.0 spec, section 4.1.4.7)
     *  This capability doesn't get page-MMIO mapped. Instead uBar, uOffset and uLength are intercepted
     *  by trapping PCI configuration I/O and get modulated by consumers to locate fetch and read/write
     *  values from any region. NOTE: The linux driver not only doesn't use this feature, it will not
     *  even list it as present if uLength isn't non-zero and 4-byte-aligned as the linux driver is
     *  initializing. */

    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_PCI_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CFG_CAP_T);
    pCfg->uCapNext = (pVirtio->fMsiSupport || pVirtio->pDevSpecificCfg) ? CFGADDR2IDX(pCfg) + pCfg->uCapLen : 0;
    pCfg->uBar     = 0;
    pCfg->uOffset  = 0;
    pCfg->uLength  = 0;
    cbRegion += pCfg->uLength;
    pVirtio->pPciCfgCap = (PVIRTIO_PCI_CFG_CAP_T)pCfg;

    if (pVirtio->pDevSpecificCfg)
    {
        /* Following capability (via VirtIO 1.0, section 4.1.4.6). Client defines the
         * device-specific config fields struct and passes size to this constructor */
        pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
        pCfg->uCfgType = VIRTIO_PCI_CAP_DEVICE_CFG;
        pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
        pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
        pCfg->uCapNext = pVirtio->fMsiSupport ? CFGADDR2IDX(pCfg) + pCfg->uCapLen : 0;
        pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
        pCfg->uOffset  = pVirtio->pIsrCap->uOffset + pVirtio->pIsrCap->uLength;
        pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);
        pCfg->uLength  = cbDevSpecificCfg;
        cbRegion += pCfg->uLength;
        pVirtio->pDeviceCap = pCfg;
    }

    if (pVirtio->fMsiSupport)
    {
        PDMMSIREG aMsiReg;
        RT_ZERO(aMsiReg);
        aMsiReg.iMsixCapOffset  = pCfg->uCapNext;
        aMsiReg.iMsixNextOffset = 0;
        aMsiReg.iMsixBar        = VIRTIO_REGION_MSIX_CAP;
        aMsiReg.cMsixVectors    = VBOX_MSIX_MAX_ENTRIES;
        rc = PDMDevHlpPCIRegisterMsi(pDevIns, &aMsiReg); /* see MsixR3init() */
        if (RT_FAILURE(rc))
        {
            /* See PDMDevHlp.cpp:pdmR3DevHlp_PCIRegisterMsi */
            Log(("Failed to configure MSI-X (%Rrc). Reverting to INTx\n", rc));
            pVirtio->fMsiSupport = false;
        }
        else
            Log(("Using MSI-X for guest driver notification\n"));
    }
    else
        Log(("MSI-X not available for VBox, using INTx notification\n"));


    /* Set offset to first capability and enable PCI dev capabilities */
    PDMPciDevSetCapabilityList(pPciDev, 0x40);
    PDMPciDevSetStatus(pPciDev,         VBOX_PCI_STATUS_CAP_LIST);

    /* Linux drivers/virtio/virtio_pci_modern.c tries to map at least a page for the
     * 'unknown' device-specific capability without querying the capability to figure
     *  out size, so pad with an extra page */

    rc = PDMDevHlpPCIIORegionRegister(pDevIns, VIRTIO_REGION_PCI_CAP,  RT_ALIGN_32(cbRegion + 0x1000, 0x1000),
                                      PCI_ADDRESS_SPACE_MEM, virtioR3Map);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pVirtio->pPrevDevSpecificCfg);
        RTMemFree(pVirtio);
        return PDMDEV_SET_ERROR(pDevIns, rc,
            N_("virtio: cannot register PCI Capabilities address space"));
    }
    *phVirtio = (VIRTIOHANDLE)pVirtio;
    return rc;
}

#ifdef IN_RING3

 /** @callback_method_impl{FNSSMDEVSAVEEXEC}  */
static DECLCALLBACK(int) virtioR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    int rc = VINF_SUCCESS;

    rc = SSMR3PutBool(pSSM,   pVirtio->fGenUpdatePending);
    rc = SSMR3PutU8(pSSM,     pVirtio->uDeviceStatus);
    rc = SSMR3PutU8(pSSM,     pVirtio->uConfigGeneration);
    rc = SSMR3PutU8(pSSM,     pVirtio->uPciCfgDataOff);
    rc = SSMR3PutU8(pSSM,     pVirtio->uISR);
    rc = SSMR3PutU16(pSSM,    pVirtio->uQueueSelect);
    rc = SSMR3PutU32(pSSM,    pVirtio->uDeviceFeaturesSelect);
    rc = SSMR3PutU32(pSSM,    pVirtio->uDriverFeaturesSelect);
    rc = SSMR3PutU32(pSSM,    pVirtio->uNumQueues);
    rc = SSMR3PutU32(pSSM,    pVirtio->cbDevSpecificCfg);
    rc = SSMR3PutU64(pSSM,    pVirtio->uDeviceFeatures);
    rc = SSMR3PutU64(pSSM,    pVirtio->uDriverFeatures);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->pDevSpecificCfg);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnVirtioStatusChanged);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnVirtioQueueNotified);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnVirtioDevCapRead);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnVirtioDevCapWrite);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnSSMDevLiveExec);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnSSMDevSaveExec);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnSSMDevLoadExec);
    rc = SSMR3PutU64(pSSM,    (uint64_t)pVirtio->virtioCallbacks.pfnSSMDevLoadDone);
    rc = SSMR3PutGCPhys(pSSM, pVirtio->pGcPhysCommonCfg);
    rc = SSMR3PutGCPhys(pSSM, pVirtio->pGcPhysNotifyCap);
    rc = SSMR3PutGCPhys(pSSM, pVirtio->pGcPhysIsrCap);
    rc = SSMR3PutGCPhys(pSSM, pVirtio->pGcPhysDeviceCap);
    rc = SSMR3PutGCPhys(pSSM, pVirtio->pGcPhysPciCapBase);

    for (uint16_t i = 0; i < pVirtio->uNumQueues; i++)
    {
        rc = SSMR3PutGCPhys64(pSSM, pVirtio->pGcPhysQueueDesc[i]);
        rc = SSMR3PutGCPhys64(pSSM, pVirtio->pGcPhysQueueAvail[i]);
        rc = SSMR3PutGCPhys64(pSSM, pVirtio->pGcPhysQueueUsed[i]);
        rc = SSMR3PutU16(pSSM,      pVirtio->uQueueNotifyOff[i]);
        rc = SSMR3PutU16(pSSM,      pVirtio->uQueueMsixVector[i]);
        rc = SSMR3PutU16(pSSM,      pVirtio->uQueueEnable[i]);
        rc = SSMR3PutU16(pSSM,      pVirtio->uQueueSize[i]);
        rc = SSMR3PutU16(pSSM,      pVirtio->virtqState[i].uAvailIdx);
        rc = SSMR3PutU16(pSSM,      pVirtio->virtqState[i].uUsedIdx);
        rc = SSMR3PutMem(pSSM,      pVirtio->virtqState[i].szVirtqName, 32);
    }

    rc = pVirtio->virtioCallbacks.pfnSSMDevSaveExec(pDevIns, pSSM);
    return rc;
}

 /** @callback_method_impl{FNSSMDEVLOADEXEC}  */
static DECLCALLBACK(int) virtioR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    RT_NOREF(uVersion);

    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    int rc = VINF_SUCCESS;

    if (uPass == SSM_PASS_FINAL)
    {
        rc = SSMR3GetBool(pSSM,  &pVirtio->fGenUpdatePending);
        rc = SSMR3GetU8(pSSM,    &pVirtio->uDeviceStatus);
        rc = SSMR3GetU8(pSSM,    &pVirtio->uConfigGeneration);
        rc = SSMR3GetU8(pSSM,    &pVirtio->uPciCfgDataOff);
        rc = SSMR3GetU8(pSSM,    &pVirtio->uISR);
        rc = SSMR3GetU16(pSSM,   &pVirtio->uQueueSelect);
        rc = SSMR3GetU32(pSSM,   &pVirtio->uDeviceFeaturesSelect);
        rc = SSMR3GetU32(pSSM,   &pVirtio->uDriverFeaturesSelect);
        rc = SSMR3GetU32(pSSM,   &pVirtio->uNumQueues);
        rc = SSMR3GetU32(pSSM,   &pVirtio->cbDevSpecificCfg);
        rc = SSMR3GetU64(pSSM,   &pVirtio->uDeviceFeatures);
        rc = SSMR3GetU64(pSSM,   &pVirtio->uDriverFeatures);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->pDevSpecificCfg);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnVirtioStatusChanged);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnVirtioQueueNotified);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnVirtioDevCapRead);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnVirtioDevCapWrite);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnSSMDevLiveExec);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnSSMDevSaveExec);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnSSMDevLoadExec);
        rc = SSMR3GetU64(pSSM,   (uint64_t *)&pVirtio->virtioCallbacks.pfnSSMDevLoadDone);
        rc = SSMR3GetGCPhys(pSSM, &pVirtio->pGcPhysCommonCfg);
        rc = SSMR3GetGCPhys(pSSM, &pVirtio->pGcPhysNotifyCap);
        rc = SSMR3GetGCPhys(pSSM, &pVirtio->pGcPhysIsrCap);
        rc = SSMR3GetGCPhys(pSSM, &pVirtio->pGcPhysDeviceCap);
        rc = SSMR3GetGCPhys(pSSM, &pVirtio->pGcPhysPciCapBase);

        for (uint16_t i = 0; i < pVirtio->uNumQueues; i++)
        {
            rc = SSMR3GetGCPhys64(pSSM, &pVirtio->pGcPhysQueueDesc[i]);
            rc = SSMR3GetGCPhys64(pSSM, &pVirtio->pGcPhysQueueAvail[i]);
            rc = SSMR3GetGCPhys64(pSSM, &pVirtio->pGcPhysQueueUsed[i]);
            rc = SSMR3GetU16(pSSM,      &pVirtio->uQueueNotifyOff[i]);
            rc = SSMR3GetU16(pSSM,      &pVirtio->uQueueMsixVector[i]);
            rc = SSMR3GetU16(pSSM,      &pVirtio->uQueueEnable[i]);
            rc = SSMR3GetU16(pSSM,      &pVirtio->uQueueSize[i]);
            rc = SSMR3GetU16(pSSM,      &pVirtio->virtqState[i].uAvailIdx);
            rc = SSMR3GetU16(pSSM,      &pVirtio->virtqState[i].uUsedIdx);
            rc = SSMR3GetMem(pSSM,      (void *)&pVirtio->virtqState[i].szVirtqName, 32);
        }
    }

    rc = pVirtio->virtioCallbacks.pfnSSMDevLoadExec(pDevIns, pSSM, uVersion, uPass);

    return rc;
}

/** @callback_method_impl{FNSSMDEVLOADDONE}  */
static DECLCALLBACK(int) virtioR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    int rc = VINF_SUCCESS;
    rc = pVirtio->virtioCallbacks.pfnSSMDevLoadDone(pDevIns, pSSM);

    return rc;
}

/** @callback_method_impl{FNSSMDEVLIVEEXEC}  */
static DECLCALLBACK(int) virtioR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    int rc = VINF_SUCCESS;
    rc = pVirtio->virtioCallbacks.pfnSSMDevLiveExec(pDevIns, pSSM, uPass);

    return rc;
}

 /**
  * Do a hex dump of a buffer
  *
  * @param   pv       Pointer to array to dump
  * @param   cb       Number of characters to dump
  * @param   uBase    Base address of offset addresses displayed
  * @param   pszTitle Header line/title for the dump
  *
  */
 void virtioHexDump(uint8_t *pv, uint32_t cb, uint32_t uBase, const char *pszTitle)
 {
     if (pszTitle)
         Log(("%s [%d bytes]:\n", pszTitle, cb));
     for (uint32_t row = 0; row < RT_MAX(1, (cb / 16) + 1) && row * 16 < cb; row++)
     {
         Log(("%04x: ", row * 16 + uBase)); /* line address */
         for (uint8_t col = 0; col < 16; col++)
         {
            uint32_t idx = row * 16 + col;
            if (idx >= cb)
                Log(("-- %s", (col + 1) % 8 ? "" : "  "));
            else
                Log(("%02x %s", pv[idx], (col + 1) % 8 ? "" : "  "));
         }
         for (uint32_t idx = row * 16; idx < row * 16 + 16; idx++)
            Log(("%c", (idx >= cb) ? ' ' : (pv[idx] >= 0x20 && pv[idx] <= 0x7e ? pv[idx] : '.')));
         Log(("\n"));
    }
    Log(("\n"));
    RT_NOREF2(uBase, pv);
 }


/**
 * Formats the logging of a memory-mapped I/O input or output value
 *
 * @param   pszFunc     - To avoid displaying this function's name via __FUNCTION__ or Log2Func()
 * @param   pszMember   - Name of struct member
 * @param   pv          - Pointer to value
 * @param   cb          - Size of value
 * @param   uOffset     - Offset into member where value starts
 * @param   fWrite      - True if write I/O
 * @param   fHasIndex   - True if the member is indexed
 * @param   idx         - The index, if fHasIndex is true
 */
void virtioLogMappedIoValue(const char *pszFunc, const char *pszMember, uint32_t uMemberSize,
                        const void *pv, uint32_t cb, uint32_t uOffset, int fWrite,
                        int fHasIndex, uint32_t idx)
{

#define FMTHEX(fmtout, val, cNybbles) \
    fmtout[cNybbles] = '\0'; \
    for (uint8_t i = 0; i < cNybbles; i++) \
        fmtout[(cNybbles - i) - 1] = "0123456789abcdef"[(val >> (i * 4)) & 0xf];

#define MAX_STRING   64
    char pszIdx[MAX_STRING] = { 0 };
    char pszDepiction[MAX_STRING] = { 0 };
    char pszFormattedVal[MAX_STRING] = { 0 };
    if (fHasIndex)
        RTStrPrintf(pszIdx, sizeof(pszIdx), "[%d]", idx);
    if (cb == 1 || cb == 2 || cb == 4 || cb == 8)
    {
        /* manually padding with 0's instead of \b due to different impl of %x precision than printf() */
        uint64_t val = 0;
        memcpy((char *)&val, pv, cb);
        FMTHEX(pszFormattedVal, val, cb * 2);
        if (uOffset != 0 || cb != uMemberSize) /* display bounds if partial member access */
            RTStrPrintf(pszDepiction, sizeof(pszDepiction), "%s%s[%d:%d]",
                        pszMember, pszIdx, uOffset, uOffset + cb - 1);
        else
            RTStrPrintf(pszDepiction, sizeof(pszDepiction), "%s%s", pszMember, pszIdx);
        RTStrPrintf(pszDepiction, sizeof(pszDepiction), "%-30s", pszDepiction);
        uint32_t first = 0;
        for (uint8_t i = 0; i < sizeof(pszDepiction); i++)
            if (pszDepiction[i] == ' ' && first++)
                pszDepiction[i] = '.';
        Log6Func(("%s: Guest %s %s 0x%s\n",
                  pszFunc, fWrite ? "wrote" : "read ", pszDepiction, pszFormattedVal));
    }
    else /* odd number or oversized access, ... log inline hex-dump style */
    {
        Log6Func(("%s: Guest %s %s%s[%d:%d]: %.*Rhxs\n",
              pszFunc, fWrite ? "wrote" : "read ", pszMember,
              pszIdx, uOffset, uOffset + cb, cb, pv));
    }
    RT_NOREF2(fWrite, pszFunc);
}

#endif /* IN_RING3 */
