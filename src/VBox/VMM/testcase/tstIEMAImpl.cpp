/* $Id$ */
/** @file
 * IEM Assembly Instruction Helper Testcase.
 */

/*
 * Copyright (C) 2022 Oracle Corporation
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
#include "../include/IEMInternal.h"

#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/mp.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @name 8-bit binary (PFNIEMAIMPLBINU8)
 * @{ */
typedef struct BINU8_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint8_t                 uDstIn;
    uint8_t                 uDstOut;
    uint8_t                 uSrcIn;
    uint8_t                 uReserved;
} BINU8_TEST_T;

typedef struct BINU8_T
{
    const char             *pszName;
    PFNIEMAIMPLBINU8        pfn;
    BINU8_TEST_T const     *paTests;
    uint32_t                cTests;
    uint32_t                uExtra;
} BINU8_T;
/** @} */


/** @name 16-bit binary (PFNIEMAIMPLBINU16)
 * @{ */
typedef struct BINU16_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint16_t                uDstIn;
    uint16_t                uDstOut;
    uint16_t                uSrcIn;
    uint16_t                uReserved;
} BINU16_TEST_T;

typedef struct BINU16_T
{
    const char             *pszName;
    PFNIEMAIMPLBINU16       pfn;
    BINU16_TEST_T const    *paTests;
    uint32_t                cTests;
    uint32_t                uExtra;
} BINU16_T;
/** @} */


/** @name 32-bit binary (PFNIEMAIMPLBINU32)
 * @{ */
typedef struct BINU32_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint32_t                uDstIn;
    uint32_t                uDstOut;
    uint32_t                uSrcIn;
    uint32_t                uReserved;
} BINU32_TEST_T;

typedef struct BINU32_T
{
    const char             *pszName;
    PFNIEMAIMPLBINU32       pfn;
    BINU32_TEST_T const    *paTests;
    uint32_t                cTests;
    uint32_t                uExtra;
} BINU32_T;
/** @} */


/** @name 64-bit binary (PFNIEMAIMPLBINU64)
 * @{ */
typedef struct BINU64_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint64_t                uDstIn;
    uint64_t                uDstOut;
    uint64_t                uSrcIn;
    uint64_t                uReserved;
} BINU64_TEST_T;

typedef struct BINU64_T
{
    const char             *pszName;
    PFNIEMAIMPLBINU64       pfn;
    BINU64_TEST_T const    *paTests;
    uint32_t                cTests;
    uint32_t                uExtra;
} BINU64_T;
/** @} */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define ENTRY(a_Name)       ENTRY_EX(a_Name, 0)
#define ENTRY_EX(a_Name, a_uExtra) \
    { #a_Name, iemAImpl_ ## a_Name, g_aTests_ ## a_Name, RT_ELEMENTS(g_aTests_ ## a_Name), a_uExtra }


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static uint32_t RandEFlags(void);
static uint8_t  RandU8(void);
static uint16_t RandU16(void);
static uint32_t RandU32(void);
static uint64_t RandU64(void);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;

#include "tstIEMAImplData.h"

/*
 * Test helpers.
 */
static const char *EFlagsDiff(uint32_t fActual, uint32_t fExpected)
{
    if (fActual == fExpected)
        return "";

    uint32_t const fXor = fActual ^ fExpected;
    static char    s_szBuf[256];
    size_t cch = RTStrPrintf(s_szBuf, sizeof(s_szBuf), " - %#x", fXor);

    static struct
    {
        const char *pszName;
        uint32_t    fFlag;
    } const s_aFlags[] =
    {
#define EFL_ENTRY(a_Flags) { #a_Flags, X86_EFL_ ## a_Flags }
        EFL_ENTRY(CF),
        EFL_ENTRY(PF),
        EFL_ENTRY(AF),
        EFL_ENTRY(ZF),
        EFL_ENTRY(SF),
        EFL_ENTRY(TF),
        EFL_ENTRY(IF),
        EFL_ENTRY(DF),
        EFL_ENTRY(OF),
        EFL_ENTRY(IOPL),
        EFL_ENTRY(NT),
        EFL_ENTRY(RF),
        EFL_ENTRY(VM),
        EFL_ENTRY(AC),
        EFL_ENTRY(VIF),
        EFL_ENTRY(VIP),
        EFL_ENTRY(ID),
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (s_aFlags[i].fFlag & fXor)
            cch += RTStrPrintf(&s_szBuf[cch], sizeof(s_szBuf) - cch,
                               s_aFlags[i].fFlag & fActual ? "/%s" : "/!%s", s_aFlags[i].pszName);
    RTStrPrintf(&s_szBuf[cch], sizeof(s_szBuf) - cch, "");
    return s_szBuf;
}


/*
 * 8-bit binary operations.
 */

#ifndef HAVE_BINU8_TESTS
static const BINU8_TEST_T g_aTests_add_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_add_u8_locked[]  = { {0} };
static const BINU8_TEST_T g_aTests_adc_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_adc_u8_locked[]  = { {0} };
static const BINU8_TEST_T g_aTests_sub_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_sub_u8_locked[]  = { {0} };
static const BINU8_TEST_T g_aTests_sbb_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_sbb_u8_locked[]  = { {0} };
static const BINU8_TEST_T g_aTests_or_u8[]          = { {0} };
static const BINU8_TEST_T g_aTests_or_u8_locked[]   = { {0} };
static const BINU8_TEST_T g_aTests_xor_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_xor_u8_locked[]  = { {0} };
static const BINU8_TEST_T g_aTests_and_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_and_u8_locked[]  = { {0} };
static const BINU8_TEST_T g_aTests_cmp_u8[]         = { {0} };
static const BINU8_TEST_T g_aTests_test_u8[]        = { {0} };
#endif

static const BINU8_T g_aBinU8[] =
{
    ENTRY(add_u8),
    ENTRY(add_u8_locked),
    ENTRY(adc_u8),
    ENTRY(adc_u8_locked),
    ENTRY(sub_u8),
    ENTRY(sub_u8_locked),
    ENTRY(sbb_u8),
    ENTRY(sbb_u8_locked),
    ENTRY(or_u8),
    ENTRY(or_u8_locked),
    ENTRY(xor_u8),
    ENTRY(xor_u8_locked),
    ENTRY(and_u8),
    ENTRY(and_u8_locked),
    ENTRY(cmp_u8),
    ENTRY(test_u8),
};


static void BinU8Generate(uint32_t cTests)
{
    RTPrintf("\n\n#define HAVE_BINU8_TESTS\n");
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU8); iFn++)
    {
        RTPrintf("static const BINU8_TEST_T g_aTests_%s[] =\n{\n", g_aBinU8[iFn].pszName);
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            BINU8_TEST_T Test;
            Test.fEflIn    = RandEFlags();
            Test.fEflOut   = Test.fEflIn;
            Test.uDstIn    = RandU8();
            Test.uDstOut   = Test.uDstIn;
            Test.uSrcIn    = RandU8();
            Test.uReserved = 0;
            g_aBinU8[iFn].pfn(&Test.uDstOut, Test.uSrcIn, &Test.fEflOut);
            RTPrintf("    { %#08x, %#08x, %#04x, %#04x, %#04x, %#x }, /* #%u */\n",
                     Test.fEflIn, Test.fEflOut, Test.uDstIn, Test.uDstOut, Test.uSrcIn, Test.uReserved, iTest);
        }
        RTPrintf("};\n");
    }
}

static void BinU8Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU8); iFn++)
    {
        RTTestSub(g_hTest, g_aBinU8[iFn].pszName);

        BINU8_TEST_T const * const paTests = g_aBinU8[iFn].paTests;
        uint32_t const             cTests  = g_aBinU8[iFn].cTests;
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            uint32_t fEfl = paTests[iTest].fEflIn;
            uint8_t  uDst = paTests[iTest].uDstIn;
            g_aBinU8[iFn].pfn(&uDst, paTests[iTest].uSrcIn, &fEfl);
            if (   uDst != paTests[iTest].uDstOut
                || fEfl != paTests[iTest].fEflOut)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#04x src=%#04x -> efl=%#08x dst=%#04x, expected %#08x & %#04x%s - %s\n",
                             iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn,
                             fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut,
                             EFlagsDiff(fEfl, paTests[iTest].fEflOut),
                             uDst == paTests[iTest].uDstOut ? "eflags" : fEfl == paTests[iTest].fEflOut ? "dst" : "both");
        }
    }
}


/*
 * 16-bit binary operations.
 */

#ifndef HAVE_BINU16_TESTS
static const BINU16_TEST_T g_aTests_add_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_add_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_adc_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_adc_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_sub_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_sub_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_sbb_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_sbb_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_or_u16[]         = { {0} };
static const BINU16_TEST_T g_aTests_or_u16_locked[]  = { {0} };
static const BINU16_TEST_T g_aTests_xor_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_xor_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_and_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_and_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_cmp_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_test_u16[]       = { {0} };
static const BINU16_TEST_T g_aTests_bt_u16[]         = { {0} };
static const BINU16_TEST_T g_aTests_btc_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_btc_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_btr_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_btr_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_bts_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_bts_u16_locked[] = { {0} };
static const BINU16_TEST_T g_aTests_bsf_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_bsr_u16[]        = { {0} };
static const BINU16_TEST_T g_aTests_imul_two_u16[]   = { {0} };
static const BINU16_TEST_T g_aTests_arpl[]           = { {0} };
#endif

static const BINU16_T g_aBinU16[] =
{
    ENTRY(add_u16),
    ENTRY(add_u16_locked),
    ENTRY(adc_u16),
    ENTRY(adc_u16_locked),
    ENTRY(sub_u16),
    ENTRY(sub_u16_locked),
    ENTRY(sbb_u16),
    ENTRY(sbb_u16_locked),
    ENTRY(or_u16),
    ENTRY(or_u16_locked),
    ENTRY(xor_u16),
    ENTRY(xor_u16_locked),
    ENTRY(and_u16),
    ENTRY(and_u16_locked),
    ENTRY(cmp_u16),
    ENTRY(test_u16),
    ENTRY_EX(bt_u16, 1),
    ENTRY_EX(btc_u16, 1),
    ENTRY_EX(btc_u16_locked, 1),
    ENTRY_EX(btr_u16, 1),
    ENTRY_EX(btr_u16_locked, 1),
    ENTRY_EX(bts_u16, 1),
    ENTRY_EX(bts_u16_locked, 1),
    ENTRY(bsf_u16),
    ENTRY(bsr_u16),
    ENTRY(imul_two_u16),
    ENTRY(arpl),
};

static void BinU16Generate(uint32_t cTests)
{
    RTPrintf("\n\n#define HAVE_BINU16_TESTS\n");
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU16); iFn++)
    {
        RTPrintf("static const BINU16_TEST_T g_aTests_%s[] =\n{\n", g_aBinU16[iFn].pszName);
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            BINU16_TEST_T Test;
            Test.fEflIn    = RandEFlags();
            Test.fEflOut   = Test.fEflIn;
            Test.uDstIn    = RandU16();
            Test.uDstOut   = Test.uDstIn;
            Test.uSrcIn    = RandU16();
            if (g_aBinU16[iFn].uExtra)
                Test.uSrcIn &= 0xf; /* Restrict bit index to a word */
            Test.uReserved = 0;
            g_aBinU16[iFn].pfn(&Test.uDstOut, Test.uSrcIn, &Test.fEflOut);
            RTPrintf("    { %#08x, %#08x, %#06x, %#06x, %#06x, %#x }, /* #%u */\n",
                     Test.fEflIn, Test.fEflOut, Test.uDstIn, Test.uDstOut, Test.uSrcIn, Test.uReserved, iTest);
        }
        RTPrintf("};\n");
    }
}

static void BinU16Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU16); iFn++)
    {
        RTTestSub(g_hTest, g_aBinU16[iFn].pszName);

        BINU16_TEST_T const * const paTests = g_aBinU16[iFn].paTests;
        uint32_t const              cTests  = g_aBinU16[iFn].cTests;
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            uint32_t fEfl = paTests[iTest].fEflIn;
            uint16_t uDst = paTests[iTest].uDstIn;
            g_aBinU16[iFn].pfn(&uDst, paTests[iTest].uSrcIn, &fEfl);
            if (   uDst != paTests[iTest].uDstOut
                || fEfl != paTests[iTest].fEflOut)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#06x src=%#06x -> efl=%#08x dst=%#06x, expected %#08x & %#06x%s - %s\n",
                             iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn,
                             fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut,
                             EFlagsDiff(fEfl, paTests[iTest].fEflOut),
                             uDst == paTests[iTest].uDstOut ? "eflags" : fEfl == paTests[iTest].fEflOut ? "dst" : "both");
        }
    }
}


/*
 * 32-bit binary operations.
 */

#ifndef HAVE_BINU32_TESTS
static const BINU32_TEST_T g_aTests_add_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_add_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_adc_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_adc_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_sub_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_sub_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_sbb_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_sbb_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_or_u32[]         = { {0} };
static const BINU32_TEST_T g_aTests_or_u32_locked[]  = { {0} };
static const BINU32_TEST_T g_aTests_xor_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_xor_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_and_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_and_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_cmp_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_test_u32[]       = { {0} };
static const BINU32_TEST_T g_aTests_bt_u32[]         = { {0} };
static const BINU32_TEST_T g_aTests_btc_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_btc_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_btr_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_btr_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_bts_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_bts_u32_locked[] = { {0} };
static const BINU32_TEST_T g_aTests_bsf_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_bsr_u32[]        = { {0} };
static const BINU32_TEST_T g_aTests_imul_two_u32[]   = { {0} };
#endif

static const BINU32_T g_aBinU32[] =
{
    ENTRY(add_u32),
    ENTRY(add_u32_locked),
    ENTRY(adc_u32),
    ENTRY(adc_u32_locked),
    ENTRY(sub_u32),
    ENTRY(sub_u32_locked),
    ENTRY(sbb_u32),
    ENTRY(sbb_u32_locked),
    ENTRY(or_u32),
    ENTRY(or_u32_locked),
    ENTRY(xor_u32),
    ENTRY(xor_u32_locked),
    ENTRY(and_u32),
    ENTRY(and_u32_locked),
    ENTRY(cmp_u32),
    ENTRY(test_u32),
    ENTRY_EX(bt_u32, 1),
    ENTRY_EX(btc_u32, 1),
    ENTRY_EX(btc_u32_locked, 1),
    ENTRY_EX(btr_u32, 1),
    ENTRY_EX(btr_u32_locked, 1),
    ENTRY_EX(bts_u32, 1),
    ENTRY_EX(bts_u32_locked, 1),
    ENTRY(bsf_u32),
    ENTRY(bsr_u32),
    ENTRY(imul_two_u32),
};

static void BinU32Generate(uint32_t cTests)
{
    RTPrintf("\n\n#define HAVE_BINU32_TESTS\n");
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU32); iFn++)
    {
        RTPrintf("static const BINU32_TEST_T g_aTests_%s[] =\n{\n", g_aBinU32[iFn].pszName);
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            BINU32_TEST_T Test;
            Test.fEflIn    = RandEFlags();
            Test.fEflOut   = Test.fEflIn;
            Test.uDstIn    = RandU32();
            Test.uDstOut   = Test.uDstIn;
            Test.uSrcIn    = RandU32();
            if (g_aBinU32[iFn].uExtra)
                Test.uSrcIn &= 0x1f; /* Restrict bit index to a word */
            Test.uReserved = 0;
            g_aBinU32[iFn].pfn(&Test.uDstOut, Test.uSrcIn, &Test.fEflOut);
            RTPrintf("    { %#08x, %#08x, %#010RX32, %#010RX32, %#010RX32, %#x }, /* #%u */\n",
                     Test.fEflIn, Test.fEflOut, Test.uDstIn, Test.uDstOut, Test.uSrcIn, Test.uReserved, iTest);
        }
        RTPrintf("};\n");
    }
}

static void BinU32Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU32); iFn++)
    {
        RTTestSub(g_hTest, g_aBinU32[iFn].pszName);

        BINU32_TEST_T const * const paTests = g_aBinU32[iFn].paTests;
        uint32_t const              cTests  = g_aBinU32[iFn].cTests;
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            uint32_t fEfl = paTests[iTest].fEflIn;
            uint32_t uDst = paTests[iTest].uDstIn;
            g_aBinU32[iFn].pfn(&uDst, paTests[iTest].uSrcIn, &fEfl);
            if (   uDst != paTests[iTest].uDstOut
                || fEfl != paTests[iTest].fEflOut)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#010RX32 src=%#010RX32 -> efl=%#08x dst=%#010RX32, expected %#08x & %#010RX32%s - %s\n",
                             iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn,
                             fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut,
                             EFlagsDiff(fEfl, paTests[iTest].fEflOut),
                             uDst == paTests[iTest].uDstOut ? "eflags" : fEfl == paTests[iTest].fEflOut ? "dst" : "both");
        }
    }
}


/*
 * 64-bit binary operations.
 */

#ifndef HAVE_BINU64_TESTS
static const BINU64_TEST_T g_aTests_add_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_add_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_adc_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_adc_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_sub_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_sub_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_sbb_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_sbb_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_or_u64[]         = { {0} };
static const BINU64_TEST_T g_aTests_or_u64_locked[]  = { {0} };
static const BINU64_TEST_T g_aTests_xor_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_xor_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_and_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_and_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_cmp_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_test_u64[]       = { {0} };
static const BINU64_TEST_T g_aTests_bt_u64[]         = { {0} };
static const BINU64_TEST_T g_aTests_btc_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_btc_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_btr_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_btr_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_bts_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_bts_u64_locked[] = { {0} };
static const BINU64_TEST_T g_aTests_bsf_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_bsr_u64[]        = { {0} };
static const BINU64_TEST_T g_aTests_imul_two_u64[]   = { {0} };
#endif

static const BINU64_T g_aBinU64[] =
{
    ENTRY(add_u64),
    ENTRY(add_u64_locked),
    ENTRY(adc_u64),
    ENTRY(adc_u64_locked),
    ENTRY(sub_u64),
    ENTRY(sub_u64_locked),
    ENTRY(sbb_u64),
    ENTRY(sbb_u64_locked),
    ENTRY(or_u64),
    ENTRY(or_u64_locked),
    ENTRY(xor_u64),
    ENTRY(xor_u64_locked),
    ENTRY(and_u64),
    ENTRY(and_u64_locked),
    ENTRY(cmp_u64),
    ENTRY(test_u64),
    ENTRY_EX(bt_u64, 1),
    ENTRY_EX(btc_u64, 1),
    ENTRY_EX(btc_u64_locked, 1),
    ENTRY_EX(btr_u64, 1),
    ENTRY_EX(btr_u64_locked, 1),
    ENTRY_EX(bts_u64, 1),
    ENTRY_EX(bts_u64_locked, 1),
    ENTRY(bsf_u64),
    ENTRY(bsr_u64),
    ENTRY(imul_two_u64),
};

static void BinU64Generate(uint32_t cTests)
{
    RTPrintf("\n\n#define HAVE_BINU64_TESTS\n");
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU64); iFn++)
    {
        RTPrintf("static const BINU64_TEST_T g_aTests_%s[] =\n{\n", g_aBinU64[iFn].pszName);
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            BINU64_TEST_T Test;
            Test.fEflIn    = RandEFlags();
            Test.fEflOut   = Test.fEflIn;
            Test.uDstIn    = RandU64();
            Test.uDstOut   = Test.uDstIn;
            Test.uSrcIn    = RandU64();
            if (g_aBinU64[iFn].uExtra)
                Test.uSrcIn &= 0x3f; /* Restrict bit index to a word */
            Test.uReserved = 0;
            g_aBinU64[iFn].pfn(&Test.uDstOut, Test.uSrcIn, &Test.fEflOut);
            RTPrintf("    { %#08x, %#08x, %#018RX64, %#018RX64, %#018RX64, %#x }, /* #%u */\n",
                     Test.fEflIn, Test.fEflOut, Test.uDstIn, Test.uDstOut, Test.uSrcIn, Test.uReserved, iTest);
        }
        RTPrintf("};\n");
    }
}

static void BinU64Test(void)
{
    for (size_t iFn = 0; iFn < RT_ELEMENTS(g_aBinU64); iFn++)
    {
        RTTestSub(g_hTest, g_aBinU64[iFn].pszName);

        BINU64_TEST_T const * const paTests = g_aBinU64[iFn].paTests;
        uint32_t const              cTests  = g_aBinU64[iFn].cTests;
        for (uint32_t iTest = 0; iTest < cTests; iTest++ )
        {
            uint32_t fEfl = paTests[iTest].fEflIn;
            uint64_t uDst = paTests[iTest].uDstIn;
            g_aBinU64[iFn].pfn(&uDst, paTests[iTest].uSrcIn, &fEfl);
            if (   uDst != paTests[iTest].uDstOut
                || fEfl != paTests[iTest].fEflOut)
                RTTestFailed(g_hTest, "#%u: efl=%#08x dst=%#018RX64 src=%#018RX64 -> efl=%#08x dst=%#018RX64, expected %#08x & %#018RX64%s - %s\n",
                             iTest, paTests[iTest].fEflIn, paTests[iTest].uDstIn, paTests[iTest].uSrcIn,
                             fEfl, uDst, paTests[iTest].fEflOut, paTests[iTest].uDstOut,
                             EFlagsDiff(fEfl, paTests[iTest].fEflOut),
                             uDst == paTests[iTest].uDstOut ? "eflags" : fEfl == paTests[iTest].fEflOut ? "dst" : "both");
        }
    }
}


/*
 * Random helpers.
 */

static uint32_t RandEFlags(void)
{
    uint32_t fEfl = RTRandU32();
    return (fEfl & X86_EFL_LIVE_MASK) | X86_EFL_RA1_MASK;
}

static uint8_t  RandU8(void)
{
    return RTRandU32Ex(0, 0xff);
}


static uint16_t  RandU16(void)
{
    return RTRandU32Ex(0, 0xffff);
}


static uint32_t  RandU32(void)
{
    return RTRandU32();
}


static uint64_t  RandU64(void)
{
    return RTRandU64();
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Generate data?
     */
    if (argc > 2)
    {
        char szCpuDesc[256] = {0};
        RTMpGetDescription(NIL_RTCPUID, szCpuDesc, sizeof(szCpuDesc));

        RTPrintf("/* $Id$ */\n"
                 "/** @file\n"
                 " * IEM Assembly Instruction Helper Testcase Data - %s.\n"
                 " */\n"
                 "\n"
                 "/*\n"
                 " * Copyright (C) 2022 Oracle Corporation\n"
                 " *\n"
                 " * This file is part of VirtualBox Open Source Edition (OSE), as\n"
                 " * available from http://www.virtualbox.org. This file is free software;\n"
                 " * you can redistribute it and/or modify it under the terms of the GNU\n"
                 " * General Public License (GPL) as published by the Free Software\n"
                 " * Foundation, in version 2 as it comes in the \"COPYING\" file of the\n"
                 " * VirtualBox OSE distribution. VirtualBox OSE is distributed in the\n"
                 " * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.\n"
                 " */\n"
                 "\n"
                 , szCpuDesc);
        uint32_t cTests = 64;
        BinU8Generate(cTests);
        BinU16Generate(cTests);
        BinU32Generate(cTests);
        BinU64Generate(cTests);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * Do testing.  Currrently disabled by default as data needs to be checked
     * on both intel and AMD systems first.
     */
    rc = RTTestCreate("tstIEMAimpl", &g_hTest);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);
    if (argc > 1)
    {
        BinU8Test();
        BinU16Test();
        BinU32Test();
        BinU64Test();
        return RTTestSummaryAndDestroy(g_hTest);
    }
    return RTTestSkipAndDestroy(g_hTest, "unfinished testcase");
}
