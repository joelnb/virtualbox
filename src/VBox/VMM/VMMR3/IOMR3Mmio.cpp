/* $Id$ */
/** @file
 * IOM - Input / Output Monitor, MMIO related APIs.
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
#define LOG_GROUP LOG_GROUP_IOM
#include <VBox/vmm/iom.h>
#include <VBox/sup.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmdev.h>
#include "IOMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/err.h>

#include "IOMInline.h"


#ifdef VBOX_WITH_STATISTICS

/**
 * Register statistics for a MMIO entry.
 */
void iomR3MmioRegStats(PVM pVM, PIOMMMIOENTRYR3 pRegEntry)
{
    PIOMMMIOSTATSENTRY   pStats      = &pVM->iom.s.paMmioStats[pRegEntry->idxStats];

    /* Format the prefix: */
    char                 szName[80];
    size_t               cchPrefix = RTStrPrintf(szName, sizeof(szName), "/IOM/NewMmio/%RGp-%RGp",
                                                 pRegEntry->GCPhysMapping, pRegEntry->GCPhysMapping + pRegEntry->cbRegion - 1);

    /* Mangle the description if this isn't the first device instance: */
    const char          *pszDesc     = pRegEntry->pszDesc;
    char                *pszFreeDesc = NULL;
    if (pRegEntry->pDevIns && pRegEntry->pDevIns->iInstance > 0 && pszDesc)
        pszDesc = pszFreeDesc = RTStrAPrintf2("%u / %s", pRegEntry->pDevIns->iInstance, pszDesc);

    /* Register statistics: */
    int rc = STAMR3Register(pVM, &pStats->Accesses,    STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES, pszDesc); AssertRC(rc);
    RTStrFree(pszFreeDesc);

# define SET_NM_SUFFIX(a_sz) memcpy(&szName[cchPrefix], a_sz, sizeof(a_sz))
    SET_NM_SUFFIX("/Read-R3");
    rc = STAMR3Register(pVM, &pStats->ProfReadR3,  STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Write-R3");
    rc = STAMR3Register(pVM, &pStats->ProfWriteR3, STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Read-RZ");
    rc = STAMR3Register(pVM, &pStats->ProfReadRZ,  STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Write-RZ");
    rc = STAMR3Register(pVM, &pStats->ProfWriteRZ, STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Read-RZtoR3");
    rc = STAMR3Register(pVM, &pStats->ReadRZToR3,  STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Write-RZtoR3");
    rc = STAMR3Register(pVM, &pStats->WriteRZToR3, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);
}


/**
 * Deregister statistics for a MMIO entry.
 */
static void iomR3MmioDeregStats(PVM pVM, PIOMMMIOENTRYR3 pRegEntry, RTGCPHYS GCPhys)
{
    char   szPrefix[80];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "/IOM/NewMmio/%RGp-%RGp/", GCPhys, GCPhys + pRegEntry->cbRegion - 1);
    STAMR3DeregisterByPrefix(pVM->pUVM, szPrefix);
}

#endif /* VBOX_WITH_STATISTICS */


/**
 * Worker for PDMDEVHLPR3::pfnMmioCreateEx.
 */
VMMR3_INT_DECL(int)  IOMR3MmioCreate(PVM pVM, PPDMDEVINS pDevIns, RTGCPHYS cbRegion, uint32_t fFlags, PPDMPCIDEV pPciDev,
                                     uint32_t iPciRegion, PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead,
                                     PFNIOMMMIONEWFILL pfnFill, void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phRegion, VERR_INVALID_POINTER);
    *phRegion = UINT32_MAX;
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    AssertPtrReturn(pDevIns, VERR_INVALID_POINTER);

    AssertMsgReturn(cbRegion > 0 && !(cbRegion & PAGE_OFFSET_MASK), ("cbRegion=%RGp\n", cbRegion), VERR_OUT_OF_RANGE);
    AssertReturn(!(fFlags & ~IOM_MMIO_F_VALID_MASK), VERR_INVALID_FLAGS);

    AssertReturn(pfnWrite || pfnRead || pfnFill, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnWrite, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnRead, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnFill, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(*pszDesc != '\0', VERR_INVALID_POINTER);
    AssertReturn(strlen(pszDesc) < 128, VERR_INVALID_POINTER);

    /*
     * Ensure that we've got table space for it.
     */
#ifndef VBOX_WITH_STATISTICS
    uint16_t const idxStats        = UINT16_MAX;
#else
    uint32_t const idxStats        = pVM->iom.s.cMmioStats;
    uint32_t const cNewMmioStats = idxStats + 1;
    AssertReturn(cNewMmioStats <= _64K, VERR_IOM_TOO_MANY_MMIO_REGISTRATIONS);
    if (cNewMmioStats > pVM->iom.s.cMmioStatsAllocation)
    {
        int rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_IOM_GROW_MMIO_STATS, cNewMmioStats, NULL);
        AssertLogRelRCReturn(rc, rc);
        AssertReturn(idxStats == pVM->iom.s.cMmioStats, VERR_IOM_MMIO_IPE_1);
        AssertReturn(cNewMmioStats <= pVM->iom.s.cMmioStatsAllocation, VERR_IOM_MMIO_IPE_2);
    }
#endif

    uint32_t idx = pVM->iom.s.cMmioRegs;
    if (idx >= pVM->iom.s.cMmioAlloc)
    {
        int rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_IOM_GROW_MMIO_REGS, pVM->iom.s.cMmioAlloc + 1, NULL);
        AssertLogRelRCReturn(rc, rc);
        AssertReturn(idx == pVM->iom.s.cMmioRegs, VERR_IOM_MMIO_IPE_1);
        AssertReturn(idx < pVM->iom.s.cMmioAlloc, VERR_IOM_MMIO_IPE_2);
    }

    /*
     * Enter it.
     */
    pVM->iom.s.paMmioRegs[idx].cbRegion           = cbRegion;
    pVM->iom.s.paMmioRegs[idx].GCPhysMapping      = NIL_RTGCPHYS;
    pVM->iom.s.paMmioRegs[idx].pvUser             = pvUser;
    pVM->iom.s.paMmioRegs[idx].pDevIns            = pDevIns;
    pVM->iom.s.paMmioRegs[idx].pfnWriteCallback   = pfnWrite;
    pVM->iom.s.paMmioRegs[idx].pfnReadCallback    = pfnRead;
    pVM->iom.s.paMmioRegs[idx].pfnFillCallback    = pfnFill;
    pVM->iom.s.paMmioRegs[idx].pszDesc            = pszDesc;
    pVM->iom.s.paMmioRegs[idx].pPciDev            = pPciDev;
    pVM->iom.s.paMmioRegs[idx].iPciRegion         = iPciRegion;
    pVM->iom.s.paMmioRegs[idx].idxStats           = (uint16_t)idxStats;
    pVM->iom.s.paMmioRegs[idx].fMapped            = false;
    pVM->iom.s.paMmioRegs[idx].fFlags             = fFlags;
    pVM->iom.s.paMmioRegs[idx].idxSelf            = idx;

    pVM->iom.s.cMmioRegs = idx + 1;
    *phRegion = idx;
    return VINF_SUCCESS;
}


/**
 * Worker for PDMDEVHLPR3::pfnMmioMap.
 */
VMMR3_INT_DECL(int)  IOMR3MmioMap(PVM pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys)
{
    /*
     * Validate input and state.
     */
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(hRegion < pVM->iom.s.cMmioRegs, VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_IOM_INVALID_MMIO_HANDLE);

    RTGCPHYS const cbRegion = pRegEntry->cbRegion;
    AssertMsgReturn(cbRegion > 0 && cbRegion <= _1P, ("cbRegion=%RGp\n", cbRegion), VERR_IOM_MMIO_IPE_1);
    AssertReturn(GCPhys + cbRegion <= GCPhys, VERR_OUT_OF_RANGE);
    RTGCPHYS const GCPhysLast = GCPhys + cbRegion - 1;

    /*
     * Do the mapping.
     */
    int rc = VINF_SUCCESS;
    IOM_LOCK_EXCL(pVM);

    if (!pRegEntry->fMapped)
    {
        uint32_t const cEntries = RT_MIN(pVM->iom.s.cMmioLookupEntries, pVM->iom.s.cMmioRegs);
        Assert(pVM->iom.s.cMmioLookupEntries == cEntries);

        PIOMMMIOLOOKUPENTRY paEntries = pVM->iom.s.paMmioLookup;
        PIOMMMIOLOOKUPENTRY pEntry;
        if (cEntries > 0)
        {
            uint32_t iFirst = 0;
            uint32_t iEnd   = cEntries;
            uint32_t i      = cEntries / 2;
            for (;;)
            {
                pEntry = &paEntries[i];
                if (pEntry->GCPhysLast < GCPhys)
                {
                    i += 1;
                    if (i < iEnd)
                        iFirst = i;
                    else
                    {
                        /* Insert after the entry we just considered: */
                        pEntry += 1;
                        if (i < cEntries)
                            memmove(pEntry + 1, pEntry, sizeof(*pEntry) * (cEntries - i));
                        break;
                    }
                }
                else if (pEntry->GCPhysFirst > GCPhysLast)
                {
                    if (i > iFirst)
                        iEnd = i;
                    else
                    {
                        /* Insert at the entry we just considered: */
                        if (i < cEntries)
                            memmove(pEntry + 1, pEntry, sizeof(*pEntry) * (cEntries - i));
                        break;
                    }
                }
                else
                {
                    /* Oops! We've got a conflict. */
                    AssertLogRelMsgFailed(("%RGp..%RGp (%s) conflicts with existing mapping %RGp..%RGp (%s)\n",
                                           GCPhys, GCPhysLast, pRegEntry->pszDesc,
                                           pEntry->GCPhysFirst, pEntry->GCPhysLast, pVM->iom.s.paMmioRegs[pEntry->idx].pszDesc));
                    IOM_UNLOCK_EXCL(pVM);
                    return VERR_IOM_MMIO_RANGE_CONFLICT;
                }

                i = iFirst + (iEnd - iFirst) / 2;
            }
        }
        else
            pEntry = paEntries;

        /*
         * Fill in the entry and bump the table size.
         */
        pEntry->idx         = hRegion;
        pEntry->GCPhysFirst = GCPhys;
        pEntry->GCPhysLast  = GCPhysLast;
        pVM->iom.s.cMmioLookupEntries = cEntries + 1;

        pRegEntry->GCPhysMapping = GCPhys;
        pRegEntry->fMapped       = true;

#ifdef VBOX_WITH_STATISTICS
        /* Don't register stats here when we're creating the VM as the
           statistics table may still be reallocated. */
        if (pVM->enmVMState >= VMSTATE_CREATED)
            iomR3MmioRegStats(pVM, pRegEntry);
#endif

#ifdef VBOX_STRICT
        /*
         * Assert table sanity.
         */
        AssertMsg(paEntries[0].GCPhysLast >= paEntries[0].GCPhysFirst, ("%RGp %RGp\n", paEntries[0].GCPhysLast, paEntries[0].GCPhysFirst));
        AssertMsg(paEntries[0].idx < pVM->iom.s.cMmioRegs, ("%#x %#x\n", paEntries[0].idx, pVM->iom.s.cMmioRegs));

        RTGCPHYS GCPhysPrev = paEntries[0].GCPhysLast;
        for (size_t i = 1; i <= cEntries; i++)
        {
            AssertMsg(paEntries[i].GCPhysLast >= paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, paEntries[i].GCPhysLast, paEntries[i].GCPhysFirst));
            AssertMsg(paEntries[i].idx < pVM->iom.s.cMmioRegs, ("%u: %#x %#x\n", i, paEntries[i].idx, pVM->iom.s.cMmioRegs));
            AssertMsg(GCPhysPrev < paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, GCPhysPrev, paEntries[i].GCPhysFirst));
            GCPhysPrev = paEntries[i].GCPhysLast;
        }
#endif
    }
    else
    {
        AssertFailed();
        rc = VERR_IOM_MMIO_REGION_ALREADY_MAPPED;
    }

    IOM_UNLOCK_EXCL(pVM);
    return rc;
}


/**
 * Worker for PDMDEVHLPR3::pfnMmioUnmap.
 */
VMMR3_INT_DECL(int)  IOMR3MmioUnmap(PVM pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    /*
     * Validate input and state.
     */
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(hRegion < pVM->iom.s.cMmioRegs, VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_IOM_INVALID_MMIO_HANDLE);

    /*
     * Do the mapping.
     */
    int rc;
    IOM_LOCK_EXCL(pVM);

    if (pRegEntry->fMapped)
    {
        RTGCPHYS const GCPhys     = pRegEntry->GCPhysMapping;
        RTGCPHYS const GCPhysLast = GCPhys + pRegEntry->cbRegion - 1;
        uint32_t const cEntries   = RT_MIN(pVM->iom.s.cMmioLookupEntries, pVM->iom.s.cMmioRegs);
        Assert(pVM->iom.s.cMmioLookupEntries == cEntries);
        Assert(cEntries > 0);

        PIOMMMIOLOOKUPENTRY paEntries = pVM->iom.s.paMmioLookup;
        uint32_t iFirst = 0;
        uint32_t iEnd   = cEntries;
        uint32_t i      = cEntries / 2;
        for (;;)
        {
            PIOMMMIOLOOKUPENTRY pEntry = &paEntries[i];
            if (pEntry->GCPhysLast < GCPhys)
            {
                i += 1;
                if (i < iEnd)
                    iFirst = i;
                else
                {
                    rc = VERR_IOM_MMIO_IPE_1;
                    AssertLogRelMsgFailedBreak(("%RGp..%RGp (%s) not found!\n", GCPhys, GCPhysLast, pRegEntry->pszDesc));
                }
            }
            else if (pEntry->GCPhysFirst > GCPhysLast)
            {
                if (i > iFirst)
                    iEnd = i;
                else
                {
                    rc = VERR_IOM_MMIO_IPE_1;
                    AssertLogRelMsgFailedBreak(("%RGp..%RGp (%s) not found!\n", GCPhys, GCPhysLast, pRegEntry->pszDesc));
                }
            }
            else if (pEntry->idx == hRegion)
            {
                Assert(pEntry->GCPhysFirst == GCPhys);
                Assert(pEntry->GCPhysLast == GCPhysLast);
#ifdef VBOX_WITH_STATISTICS
                iomR3MmioDeregStats(pVM, pRegEntry, GCPhys);
#endif
                if (i + 1 < cEntries)
                    memmove(pEntry, pEntry + 1, sizeof(*pEntry) * (cEntries - i - 1));
                pVM->iom.s.cMmioLookupEntries = cEntries - 1;
                pRegEntry->GCPhysMapping = NIL_RTGCPHYS;
                pRegEntry->fMapped       = false;
                rc = VINF_SUCCESS;
                break;
            }
            else
            {
                AssertLogRelMsgFailed(("Lookig for %RGp..%RGp (%s), found %RGp..%RGp (%s) instead!\n",
                                       GCPhys, GCPhysLast, pRegEntry->pszDesc,
                                       pEntry->GCPhysFirst, pEntry->GCPhysLast, pVM->iom.s.paMmioRegs[pEntry->idx].pszDesc));
                rc = VERR_IOM_MMIO_IPE_1;
                break;
            }

            i = iFirst + (iEnd - iFirst) / 2;
        }

#ifdef VBOX_STRICT
        /*
         * Assert table sanity.
         */
        AssertMsg(paEntries[0].GCPhysLast >= paEntries[0].GCPhysFirst, ("%RGp %RGp\n", paEntries[0].GCPhysLast, paEntries[0].GCPhysFirst));
        AssertMsg(paEntries[0].idx < pVM->iom.s.cMmioRegs, ("%#x %#x\n", paEntries[0].idx, pVM->iom.s.cMmioRegs));

        RTGCPHYS GCPhysPrev = paEntries[0].GCPhysLast;
        for (i = 1; i < cEntries - 1; i++)
        {
            AssertMsg(paEntries[i].GCPhysLast >= paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, paEntries[i].GCPhysLast, paEntries[i].GCPhysFirst));
            AssertMsg(paEntries[i].idx < pVM->iom.s.cMmioRegs, ("%u: %#x %#x\n", i, paEntries[i].idx, pVM->iom.s.cMmioRegs));
            AssertMsg(GCPhysPrev < paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, GCPhysPrev, paEntries[i].GCPhysFirst));
            GCPhysPrev = paEntries[i].GCPhysLast;
        }
#endif
    }
    else
    {
        AssertFailed();
        rc = VERR_IOM_MMIO_REGION_NOT_MAPPED;
    }

    IOM_UNLOCK_EXCL(pVM);
    return rc;
}


VMMR3_INT_DECL(int)  IOMR3MmioReduce(PVM pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS cbRegion)
{
    RT_NOREF(pVM, pDevIns, hRegion, cbRegion);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Display a single MMIO range.
 *
 * @returns 0
 * @param   pNode   Pointer to MMIO R3 range.
 * @param   pvUser  Pointer to info output callback structure.
 */
static DECLCALLBACK(int) iomR3MmioInfoOne(PAVLROGCPHYSNODECORE pNode, void *pvUser)
{
    PIOMMMIORANGE pRange = (PIOMMMIORANGE)pNode;
    PCDBGFINFOHLP pHlp = (PCDBGFINFOHLP)pvUser;
    pHlp->pfnPrintf(pHlp,
                    "%RGp-%RGp %RHv %RHv %RHv %RHv %RHv %s\n",
                    pRange->Core.Key,
                    pRange->Core.KeyLast,
                    pRange->pDevInsR3,
                    pRange->pfnReadCallbackR3,
                    pRange->pfnWriteCallbackR3,
                    pRange->pfnFillCallbackR3,
                    pRange->pvUserR3,
                    pRange->pszDesc);
    pHlp->pfnPrintf(pHlp,
                    "%*s %RHv %RHv %RHv %RHv %RHv\n",
                    sizeof(RTGCPHYS) * 2 * 2 + 1, "R0",
                    pRange->pDevInsR0,
                    pRange->pfnReadCallbackR0,
                    pRange->pfnWriteCallbackR0,
                    pRange->pfnFillCallbackR0,
                    pRange->pvUserR0);
#if 0
    pHlp->pfnPrintf(pHlp,
                    "%*s %RRv %RRv %RRv %RRv %RRv\n",
                    sizeof(RTGCPHYS) * 2 * 2 + 1, "RC",
                    pRange->pDevInsRC,
                    pRange->pfnReadCallbackRC,
                    pRange->pfnWriteCallbackRC,
                    pRange->pfnFillCallbackRC,
                    pRange->pvUserRC);
#endif
    return 0;
}


/**
 * Display all registered MMIO ranges.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
DECLCALLBACK(void) iomR3MmioInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /* No locking needed here as registerations are only happening during VMSTATE_CREATING. */
    pHlp->pfnPrintf(pHlp,
                    "MMIO registrations: %u (%u allocated)\n"
                    " ## Ctx    %.*s %.*s   PCI    Description\n",
                    pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc,
                    sizeof(RTGCPHYS) * 2, "Size",
                    sizeof(RTGCPHYS) * 2 * 2 + 1, "Mapping");
    PIOMMMIOENTRYR3 paRegs = pVM->iom.s.paMmioRegs;
    for (uint32_t i = 0; i < pVM->iom.s.cMmioRegs; i++)
    {
        const char * const pszRing = paRegs[i].fRing0 ? paRegs[i].fRawMode ? "+0+C" : "+0  "
                                   : paRegs[i].fRawMode ? "+C  " : "    ";
        if (paRegs[i].fMapped && paRegs[i].pPciDev)
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %RGp-%RGp pci%u/%u %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            paRegs[i].GCPhysMapping, paRegs[i].GCPhysMapping + paRegs[i].cbRegion - 1,
                            paRegs[i].pPciDev->idxSubDev, paRegs[i].iPciRegion, paRegs[i].pszDesc);
        else if (paRegs[i].fMapped && !paRegs[i].pPciDev)
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %RGp-%RGp        %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            paRegs[i].GCPhysMapping, paRegs[i].GCPhysMapping + paRegs[i].cbRegion - 1, paRegs[i].pszDesc);
        else if (paRegs[i].pPciDev)
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %.*s pci%u/%u %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            sizeof(RTGCPHYS) * 2, "unmapped", paRegs[i].pPciDev->idxSubDev, paRegs[i].iPciRegion, paRegs[i].pszDesc);
        else
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %.*s        %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            sizeof(RTGCPHYS) * 2, "unmapped", paRegs[i].pszDesc);
    }

    /* Legacy registration: */
    NOREF(pszArgs);
    pHlp->pfnPrintf(pHlp,
                    "MMIO ranges (pVM=%p)\n"
                    "%.*s %.*s %.*s %.*s %.*s %.*s %s\n",
                    pVM,
                    sizeof(RTGCPHYS) * 4 + 1, "GC Phys Range                    ",
                    sizeof(RTHCPTR) * 2,      "pDevIns         ",
                    sizeof(RTHCPTR) * 2,      "Read            ",
                    sizeof(RTHCPTR) * 2,      "Write           ",
                    sizeof(RTHCPTR) * 2,      "Fill            ",
                    sizeof(RTHCPTR) * 2,      "pvUser          ",
                                              "Description");
    IOM_LOCK_SHARED(pVM);
    RTAvlroGCPhysDoWithAll(&pVM->iom.s.pTreesR3->MMIOTree, true, iomR3MmioInfoOne, (void *)pHlp);
    IOM_UNLOCK_SHARED(pVM);
}

