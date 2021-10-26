/* $Id$ */
/** @file
 * VBox - Page Manager, Guest Paging Template - All context code.
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


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_EPT \
 || PGM_GST_TYPE == PGM_TYPE_AMD64
DECLINLINE(int) PGM_GST_NAME(Walk)(PVMCPUCC pVCpu, RTGCPTR GCPtr, PGSTPTWALK pWalk);
#endif
PGM_GST_DECL(int,  GetPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys);
PGM_GST_DECL(int,  ModifyPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask);

#ifdef IN_RING3 /* r3 only for now.  */
PGM_GST_DECL(int, Enter)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3);
PGM_GST_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta);
PGM_GST_DECL(int, Exit)(PVMCPUCC pVCpu);
#endif
RT_C_DECLS_END


/**
 * Enters the guest mode.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPhysCR3   The physical address from the CR3 register.
 */
PGM_GST_DECL(int, Enter)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3)
{
    /*
     * Map and monitor CR3
     */
    uintptr_t idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnMapCR3, VERR_PGM_MODE_IPE);
    return g_aPgmBothModeData[idxBth].pfnMapCR3(pVCpu, GCPhysCR3, false /* fPdpesMapped */);
}


/**
 * Exits the guest mode.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
PGM_GST_DECL(int, Exit)(PVMCPUCC pVCpu)
{
    uintptr_t idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnUnmapCR3, VERR_PGM_MODE_IPE);
    return g_aPgmBothModeData[idxBth].pfnUnmapCR3(pVCpu);
}


#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_EPT \
 || PGM_GST_TYPE == PGM_TYPE_AMD64


DECLINLINE(int) PGM_GST_NAME(WalkReturnNotPresent)(PVMCPUCC pVCpu, PGSTPTWALK pWalk, int iLevel)
{
    NOREF(iLevel); NOREF(pVCpu);
    pWalk->Core.fNotPresent     = true;
    pWalk->Core.uLevel          = (uint8_t)iLevel;
    return VERR_PAGE_TABLE_NOT_PRESENT;
}

DECLINLINE(int) PGM_GST_NAME(WalkReturnBadPhysAddr)(PVMCPUCC pVCpu, PGSTPTWALK pWalk, int iLevel, int rc)
{
    AssertMsg(rc == VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS, ("%Rrc\n", rc)); NOREF(rc); NOREF(pVCpu);
    pWalk->Core.fBadPhysAddr    = true;
    pWalk->Core.uLevel          = (uint8_t)iLevel;
    return VERR_PAGE_TABLE_NOT_PRESENT;
}

DECLINLINE(int) PGM_GST_NAME(WalkReturnRsvdError)(PVMCPUCC pVCpu, PGSTPTWALK pWalk, int iLevel)
{
    NOREF(pVCpu);
    pWalk->Core.fRsvdError      = true;
    pWalk->Core.uLevel          = (uint8_t)iLevel;
    return VERR_PAGE_TABLE_NOT_PRESENT;
}


/**
 * Performs a guest page table walk.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT on failure.  Check pWalk for details.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       The guest virtual address to walk by.
 * @param   pWalk       Where to return the walk result. This is always set.
 */
DECLINLINE(int) PGM_GST_NAME(Walk)(PVMCPUCC pVCpu, RTGCPTR GCPtr, PGSTPTWALK pWalk)
{
    int rc;

    /*
     * Init the walking structure.
     */
    RT_ZERO(*pWalk);
    pWalk->Core.GCPtr = GCPtr;

# if PGM_GST_TYPE == PGM_TYPE_32BIT \
  || PGM_GST_TYPE == PGM_TYPE_PAE
    /*
     * Boundary check for PAE and 32-bit (prevents trouble further down).
     */
    if (RT_UNLIKELY(GCPtr >= _4G))
        return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 8);
# endif

    uint32_t fEffective = X86_PTE_RW | X86_PTE_US | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_A | 1;
    {
# if PGM_GST_TYPE == PGM_TYPE_AMD64
        /*
         * The PMLE4.
         */
        rc = pgmGstGetLongModePML4PtrEx(pVCpu, &pWalk->pPml4);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 4, rc);

        PX86PML4E pPml4e;
        pWalk->pPml4e  = pPml4e  = &pWalk->pPml4->a[(GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK];
        X86PML4E  Pml4e;
        pWalk->Pml4e.u = Pml4e.u = pPml4e->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pml4e)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 4);

        if (RT_LIKELY(GST_IS_PML4E_VALID(pVCpu, Pml4e))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 4);

        pWalk->Core.fEffective = fEffective = ((uint32_t)Pml4e.u & (X86_PML4E_RW  | X86_PML4E_US | X86_PML4E_PWT | X86_PML4E_PCD | X86_PML4E_A))
                                            | ((uint32_t)(Pml4e.u >> 63) ^ 1) /*NX */;

        /*
         * The PDPE.
         */
        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, Pml4e.u & X86_PML4E_PG_MASK, &pWalk->pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 3, rc);

# elif PGM_GST_TYPE == PGM_TYPE_PAE
        rc = pgmGstGetPaePDPTPtrEx(pVCpu, &pWalk->pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 8, rc);

# elif PGM_GST_TYPE == PGM_TYPE_EPT
        rc = pgmGstGetEptPML4PtrEx(pVCpu, &pWalk->pPml4);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 4, rc);

        PEPTPML4E pPml4e;
        pWalk->pPml4e = pPml4e = &pWalk->pPml4->a[(GCPtr >> EPT_PML4_SHIFT) & EPT_PML4_MASK];
        EPTPML4E  Pml4e;
        pWalk->Pml4e.u = Pml4e.u = pPml4e->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pml4e)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 4);

        if (RT_LIKELY(GST_IS_PML4E_VALID(pVCpu, Pml4e))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 4);

        Assert(!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmxModeBasedExecuteEpt);
        uint64_t const fEptAttrs     = Pml4e.u & EPT_PML4E_ATTR_MASK;
        uint8_t const fExecute       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
        uint8_t const fRead          = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
        uint8_t const fWrite         = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
        uint8_t const fAccessed      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
        uint32_t const fEffectiveEpt = ((uint32_t)fEptAttrs << PGMPTWALK_EFF_EPT_ATTR_SHIFT) & PGMPTWALK_EFF_EPT_ATTR_MASK;
        pWalk->Core.fEffective = fEffective = RT_BF_MAKE(PGM_BF_PTWALK_EFF_X,  fExecute)
                                            | RT_BF_MAKE(PGM_BF_PTWALK_EFF_RW, fRead & fWrite)
                                            | RT_BF_MAKE(PGM_BF_PTWALK_EFF_US, 1)
                                            | RT_BF_MAKE(PGM_BF_PTWALK_EFF_A,  fAccessed)
                                            | fEffectiveEpt;

        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, Pml4e.u & EPT_PML4E_PG_MASK, &pWalk->pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 3, rc);
# endif
    }
    {
# if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
        PX86PDPE pPdpe;
        pWalk->pPdpe  = pPdpe  = &pWalk->pPdpt->a[(GCPtr >> GST_PDPT_SHIFT) & GST_PDPT_MASK];
        X86PDPE  Pdpe;
        pWalk->Pdpe.u = Pdpe.u = pPdpe->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pdpe)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 3);

        if (RT_LIKELY(GST_IS_PDPE_VALID(pVCpu, Pdpe))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 3);

# if PGM_GST_TYPE == PGM_TYPE_AMD64
        pWalk->Core.fEffective = fEffective &= ((uint32_t)Pdpe.u & (X86_PDPE_RW  | X86_PDPE_US | X86_PDPE_PWT | X86_PDPE_PCD | X86_PDPE_A))
                                             | ((uint32_t)(Pdpe.u >> 63) ^ 1) /*NX */;
# else
        pWalk->Core.fEffective = fEffective  = X86_PDPE_RW  | X86_PDPE_US | X86_PDPE_A
                                             | ((uint32_t)Pdpe.u & (X86_PDPE_PWT | X86_PDPE_PCD))
                                             | ((uint32_t)(Pdpe.u >> 63) ^ 1) /*NX */;
# endif

        /*
         * The PDE.
         */
        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, Pdpe.u & X86_PDPE_PG_MASK, &pWalk->pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 2, rc);
# elif PGM_GST_TYPE == PGM_TYPE_32BIT
        rc = pgmGstGet32bitPDPtrEx(pVCpu, &pWalk->pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 8, rc);

# elif PGM_GST_TYPE == PGM_TYPE_EPT
        PEPTPDPTE pPdpte;
        pWalk->pPdpte = pPdpte = &pWalk->pPdpt->a[(GCPtr >> GST_PDPT_SHIFT) & GST_PDPT_MASK];
        EPTPDPTE  Pdpte;
        pWalk->Pdpte.u = Pdpte.u = pPdpte->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pdpte)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 3);

        /* The order of the following 2 "if" statements matter. */
        if (GST_IS_PDPE_VALID(pVCpu, Pdpte))
        {
            uint64_t const fEptAttrs = Pdpte.u & EPT_PDPTE_ATTR_MASK;
            uint8_t const fExecute   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const fWrite     = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const fAccessed  = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint32_t const fEffectiveEpt = ((uint32_t)fEptAttrs << PGMPTWALK_EFF_EPT_ATTR_SHIFT) & PGMPTWALK_EFF_EPT_ATTR_MASK;
            pWalk->Core.fEffective = fEffective &= RT_BF_MAKE(PGM_BF_PTWALK_EFF_X,  fExecute)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_RW, fWrite)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_US, 1)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_A,  fAccessed)
                                                 | fEffectiveEpt;
        }
        else if (GST_IS_BIG_PDPE_VALID(pVCpu, Pdpte))
        {
            uint64_t const fEptAttrs  = Pdpte.u & EPT_PDPTE1G_ATTR_MASK;
            uint8_t const fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint8_t const fDirty      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_DIRTY);
            uint32_t const fEffectiveEpt = ((uint32_t)fEptAttrs << PGMPTWALK_EFF_EPT_ATTR_SHIFT) & PGMPTWALK_EFF_EPT_ATTR_MASK;
            pWalk->Core.fEffective = fEffective &= RT_BF_MAKE(PGM_BF_PTWALK_EFF_X,       fExecute)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_RW,      fWrite)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_US,      1)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_A,       fAccessed)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_D,       fDirty)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_MEMTYPE, 0)
                                                 | fEffectiveEpt;
            pWalk->Core.fEffectiveRW = !!(fEffective & X86_PTE_RW);
            pWalk->Core.fEffectiveUS = true;
            pWalk->Core.fEffectiveNX = !fExecute;
            pWalk->Core.fGigantPage  = true;
            pWalk->Core.fSucceeded   = true;
            pWalk->Core.GCPhys       = GST_GET_BIG_PDPE_GCPHYS(pVCpu->CTX_SUFF(pVM), Pdpte)
                                     | (GCPtr & GST_GIGANT_PAGE_OFFSET_MASK);
            PGM_A20_APPLY_TO_VAR(pVCpu, pWalk->Core.GCPhys);
            return VINF_SUCCESS;
        }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 3);
# endif
    }
    {
        PGSTPDE pPde;
        pWalk->pPde  = pPde  = &pWalk->pPd->a[(GCPtr >> GST_PD_SHIFT) & GST_PD_MASK];
        GSTPDE  Pde;
        pWalk->Pde.u = Pde.u = pPde->u;
        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pde)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 2);
        if ((Pde.u & X86_PDE_PS) && GST_IS_PSE_ACTIVE(pVCpu))
        {
            if (RT_LIKELY(GST_IS_BIG_PDE_VALID(pVCpu, Pde))) { /* likely */ }
            else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 2);

            /*
             * We're done.
             */
# if PGM_GST_TYPE == PGM_TYPE_EPT
            uint64_t const fEptAttrs  = Pde.u & EPT_PDE2M_ATTR_MASK;
            uint8_t const fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint8_t const fDirty      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_DIRTY);
            uint32_t fEffectiveEpt = ((uint32_t)fEptAttrs << PGMPTWALK_EFF_EPT_ATTR_SHIFT) & PGMPTWALK_EFF_EPT_ATTR_MASK;
            pWalk->Core.fEffective = fEffective &= RT_BF_MAKE(PGM_BF_PTWALK_EFF_X,       fExecute)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_RW,      fWrite)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_US,      1)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_A,       fAccessed)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_D,       fDirty)
                                                 | RT_BF_MAKE(PGM_BF_PTWALK_EFF_MEMTYPE, 0)
                                                 | fEffectiveEpt;
            pWalk->Core.fEffectiveRW = !!(fEffective & X86_PTE_RW);
            pWalk->Core.fEffectiveUS = true;
            pWalk->Core.fEffectiveNX = !fExecute;
# else
#  if PGM_GST_TYPE == PGM_TYPE_32BIT
             fEffective &= Pde.u & (X86_PDE4M_RW  | X86_PDE4M_US | X86_PDE4M_PWT | X86_PDE4M_PCD | X86_PDE4M_A);
#  else
             fEffective &= ((uint32_t)Pde.u & (X86_PDE4M_RW  | X86_PDE4M_US | X86_PDE4M_PWT | X86_PDE4M_PCD | X86_PDE4M_A))
                         | ((uint32_t)(Pde.u >> 63) ^ 1) /*NX */;
#  endif
             fEffective |= (uint32_t)Pde.u & (X86_PDE4M_D | X86_PDE4M_G);
             fEffective |= (uint32_t)(Pde.u & X86_PDE4M_PAT) >> X86_PDE4M_PAT_SHIFT;
             pWalk->Core.fEffective = fEffective;

             pWalk->Core.fEffectiveRW = !!(fEffective & X86_PTE_RW);
             pWalk->Core.fEffectiveUS = !!(fEffective & X86_PTE_US);
#  if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
             pWalk->Core.fEffectiveNX = !(fEffective & 1) && GST_IS_NX_ACTIVE(pVCpu);
#  else
             pWalk->Core.fEffectiveNX = false;
#  endif
# endif
            pWalk->Core.fBigPage     = true;
            pWalk->Core.fSucceeded   = true;

            pWalk->Core.GCPhys       = GST_GET_BIG_PDE_GCPHYS(pVCpu->CTX_SUFF(pVM), Pde)
                                     | (GCPtr & GST_BIG_PAGE_OFFSET_MASK);
            PGM_A20_APPLY_TO_VAR(pVCpu, pWalk->Core.GCPhys);
            return VINF_SUCCESS;
        }

        if (RT_UNLIKELY(!GST_IS_PDE_VALID(pVCpu, Pde)))
            return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 2);
# if  PGM_GST_TYPE == PGM_TYPE_EPT
        uint64_t const fEptAttrs = Pde.u & EPT_PDE_ATTR_MASK;
        uint8_t const fExecute   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
        uint8_t const fWrite     = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
        uint8_t const fAccessed  = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
        uint32_t const fEffectiveEpt = ((uint32_t)fEptAttrs << PGMPTWALK_EFF_EPT_ATTR_SHIFT) & PGMPTWALK_EFF_EPT_ATTR_MASK;
        pWalk->Core.fEffective = fEffective &= RT_BF_MAKE(PGM_BF_PTWALK_EFF_X,  fExecute)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_RW, fWrite)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_US, 1)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_A,  fAccessed)
                                             | fEffectiveEpt;
# elif PGM_GST_TYPE == PGM_TYPE_32BIT
        pWalk->Core.fEffective = fEffective &= Pde.u & (X86_PDE_RW  | X86_PDE_US | X86_PDE_PWT | X86_PDE_PCD | X86_PDE_A);
# else
        pWalk->Core.fEffective = fEffective &= ((uint32_t)Pde.u & (X86_PDE_RW  | X86_PDE_US | X86_PDE_PWT | X86_PDE_PCD | X86_PDE_A))
                                             | ((uint32_t)(Pde.u >> 63) ^ 1) /*NX */;
# endif

        /*
         * The PTE.
         */
        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, GST_GET_PDE_GCPHYS(Pde), &pWalk->pPt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 1, rc);
    }
    {
        PGSTPTE pPte;
        pWalk->pPte  = pPte  = &pWalk->pPt->a[(GCPtr >> GST_PT_SHIFT) & GST_PT_MASK];
        GSTPTE  Pte;
        pWalk->Pte.u = Pte.u = pPte->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pte)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 1);

        if (RT_LIKELY(GST_IS_PTE_VALID(pVCpu, Pte))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 1);

        /*
         * We're done.
         */
# if PGM_GST_TYPE == PGM_TYPE_EPT
        uint64_t const fEptAttrs  = Pte.u & EPT_PTE_ATTR_MASK;
        uint8_t const fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
        uint8_t const fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
        uint8_t const fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
        uint8_t const fDirty      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_DIRTY);
        uint32_t fEffectiveEpt = ((uint32_t)fEptAttrs << PGMPTWALK_EFF_EPT_ATTR_SHIFT) & PGMPTWALK_EFF_EPT_ATTR_MASK;
        pWalk->Core.fEffective = fEffective &= RT_BF_MAKE(PGM_BF_PTWALK_EFF_X,       fExecute)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_RW,      fWrite)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_US,      1)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_A,       fAccessed)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_D,       fDirty)
                                             | RT_BF_MAKE(PGM_BF_PTWALK_EFF_MEMTYPE, 0)
                                             | fEffectiveEpt;
        pWalk->Core.fEffectiveRW = !!(fEffective & X86_PTE_RW);
        pWalk->Core.fEffectiveUS = true;
        pWalk->Core.fEffectiveNX = !fExecute;
# else
#  if PGM_GST_TYPE == PGM_TYPE_32BIT
        fEffective &= Pte.u & (X86_PTE_RW  | X86_PTE_US | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_A);
#  else
        fEffective &= ((uint32_t)Pte.u & (X86_PTE_RW  | X86_PTE_US | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_A))
                    | ((uint32_t)(Pte.u >> 63) ^ 1) /*NX */;
#  endif
        fEffective |= (uint32_t)Pte.u & (X86_PTE_D | X86_PTE_PAT | X86_PTE_G);
        pWalk->Core.fEffective = fEffective;

        pWalk->Core.fEffectiveRW = !!(fEffective & X86_PTE_RW);
#  if PGM_GST_TYPE == PGM_TYPE_EPT
        pWalk->Core.fEffectiveUS = true;
#  else
        pWalk->Core.fEffectiveUS = !!(fEffective & X86_PTE_US);
#  endif
#  if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
        pWalk->Core.fEffectiveNX = !(fEffective & 1) && GST_IS_NX_ACTIVE(pVCpu);
#  else
        pWalk->Core.fEffectiveNX = false;
#  endif
# endif
        pWalk->Core.fSucceeded   = true;
        pWalk->Core.GCPhys       = GST_GET_PDE_GCPHYS(Pte)      /** @todo Shouldn't this be PTE_GCPHYS? */
                                 | (GCPtr & PAGE_OFFSET_MASK);
        return VINF_SUCCESS;
    }
}

#endif /* 32BIT, PAE, EPT, AMD64 */

/**
 * Gets effective Guest OS page information.
 *
 * When GCPtr is in a big page, the function will return as if it was a normal
 * 4KB page. If the need for distinguishing between big and normal page becomes
 * necessary at a later point, a PGMGstGetPage Ex() will be created for that
 * purpose.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pfFlags     Where to store the flags. These are X86_PTE_*, even for big pages.
 * @param   pGCPhys     Where to store the GC physical address of the page.
 *                      This is page aligned!
 */
PGM_GST_DECL(int, GetPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint64_t *pfFlags, PRTGCPHYS pGCPhys)
{
#if PGM_GST_TYPE == PGM_TYPE_REAL \
 || PGM_GST_TYPE == PGM_TYPE_PROT
    /*
     * Fake it.
     */
    if (pfFlags)
        *pfFlags = X86_PTE_P | X86_PTE_RW | X86_PTE_US;
    if (pGCPhys)
        *pGCPhys = GCPtr & PAGE_BASE_GC_MASK;
    NOREF(pVCpu);
    return VINF_SUCCESS;

#elif PGM_GST_TYPE == PGM_TYPE_32BIT \
   || PGM_GST_TYPE == PGM_TYPE_PAE \
   || PGM_GST_TYPE == PGM_TYPE_EPT \
   || PGM_GST_TYPE == PGM_TYPE_AMD64

    GSTPTWALK Walk;
    int rc = PGM_GST_NAME(Walk)(pVCpu, GCPtr, &Walk);
    if (RT_FAILURE(rc))
        return rc;

    if (pGCPhys)
        *pGCPhys = Walk.Core.GCPhys & ~(RTGCPHYS)PAGE_OFFSET_MASK;

    if (pfFlags)
    {
        if (!Walk.Core.fBigPage)
            *pfFlags = (Walk.Pte.u & ~(GST_PTE_PG_MASK | X86_PTE_RW | X86_PTE_US))                      /* NX not needed */
                     | (Walk.Core.fEffectiveRW ? X86_PTE_RW : 0)
                     | (Walk.Core.fEffectiveUS ? X86_PTE_US : 0)
# if PGM_WITH_NX(PGM_GST_TYPE, PGM_GST_TYPE)
                     | (Walk.Core.fEffectiveNX ? X86_PTE_PAE_NX : 0)
# endif
                     ;
        else
        {
            *pfFlags = (Walk.Pde.u & ~(GST_PTE_PG_MASK | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PS))   /* NX not needed */
                     | ((Walk.Pde.u & X86_PDE4M_PAT) >> X86_PDE4M_PAT_SHIFT)
                     | (Walk.Core.fEffectiveRW ? X86_PTE_RW : 0)
                     | (Walk.Core.fEffectiveUS ? X86_PTE_US : 0)
# if PGM_WITH_NX(PGM_GST_TYPE, PGM_GST_TYPE)
                     | (Walk.Core.fEffectiveNX ? X86_PTE_PAE_NX : 0)
# endif
                     ;
        }
    }

    return VINF_SUCCESS;

#else
# error "shouldn't be here!"
    /* something else... */
    return VERR_NOT_SUPPORTED;
#endif
}


/**
 * Modify page flags for a range of pages in the guest's tables
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range. Page aligned!
 * @param   cb          Size (in bytes) of the page range to apply the modification to. Page aligned!
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*.
 */
PGM_GST_DECL(int, ModifyPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
{
    Assert((cb & PAGE_OFFSET_MASK) == 0); RT_NOREF_PV(cb);

#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_AMD64
    for (;;)
    {
        GSTPTWALK Walk;
        int rc = PGM_GST_NAME(Walk)(pVCpu, GCPtr, &Walk);
        if (RT_FAILURE(rc))
            return rc;

        if (!Walk.Core.fBigPage)
        {
            /*
             * 4KB Page table, process
             *
             * Walk pages till we're done.
             */
            unsigned iPTE = (GCPtr >> GST_PT_SHIFT) & GST_PT_MASK;
            while (iPTE < RT_ELEMENTS(Walk.pPt->a))
            {
                GSTPTE Pte = Walk.pPt->a[iPTE];
                Pte.u = (Pte.u & (fMask | X86_PTE_PAE_PG_MASK))
                      | (fFlags & ~GST_PTE_PG_MASK);
                Walk.pPt->a[iPTE] = Pte;

                /* next page */
                cb -= PAGE_SIZE;
                if (!cb)
                    return VINF_SUCCESS;
                GCPtr += PAGE_SIZE;
                iPTE++;
            }
        }
        else
        {
            /*
             * 2/4MB Page table
             */
            GSTPDE PdeNew;
# if PGM_GST_TYPE == PGM_TYPE_32BIT
            PdeNew.u = (Walk.Pde.u & (fMask | ((fMask & X86_PTE_PAT) << X86_PDE4M_PAT_SHIFT) | GST_PDE_BIG_PG_MASK | X86_PDE4M_PG_HIGH_MASK | X86_PDE4M_PS))
# else
            PdeNew.u = (Walk.Pde.u & (fMask | ((fMask & X86_PTE_PAT) << X86_PDE4M_PAT_SHIFT) | GST_PDE_BIG_PG_MASK | X86_PDE4M_PS))
# endif
                     | (fFlags & ~GST_PTE_PG_MASK)
                     | ((fFlags & X86_PTE_PAT) << X86_PDE4M_PAT_SHIFT);
            *Walk.pPde = PdeNew;

            /* advance */
            const unsigned cbDone = GST_BIG_PAGE_SIZE - (GCPtr & GST_BIG_PAGE_OFFSET_MASK);
            if (cbDone >= cb)
                return VINF_SUCCESS;
            cb    -= cbDone;
            GCPtr += cbDone;
        }
    }

#else
    /* real / protected mode: ignore. */
    NOREF(pVCpu); NOREF(GCPtr); NOREF(fFlags); NOREF(fMask);
    return VINF_SUCCESS;
#endif
}


#ifdef IN_RING3
/**
 * Relocate any GC pointers related to guest mode paging.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offDelta    The relocation offset.
 */
PGM_GST_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta)
{
    RT_NOREF(pVCpu, offDelta);
    return VINF_SUCCESS;
}
#endif
