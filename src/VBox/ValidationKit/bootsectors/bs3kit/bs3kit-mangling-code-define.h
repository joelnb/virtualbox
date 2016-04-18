/* $Id$ */
/** @file
 * BS3Kit - Function needing mangling.
 */

/*
 * Copyright (C) 2007-2016 Oracle Corporation
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


#define Bs3A20Disable                           BS3_CMN_MANGLER(Bs3A20Disable)
#define Bs3A20DisableViaKbd                     BS3_CMN_MANGLER(Bs3A20DisableViaKbd)
#define Bs3A20DisableViaPortA                   BS3_CMN_MANGLER(Bs3A20DisableViaPortA)
#define Bs3A20Enable                            BS3_CMN_MANGLER(Bs3A20Enable)
#define Bs3A20EnableViaKbd                      BS3_CMN_MANGLER(Bs3A20EnableViaKbd)
#define Bs3A20EnableViaPortA                    BS3_CMN_MANGLER(Bs3A20EnableViaPortA)
#define Bs3KbdRead                              BS3_CMN_MANGLER(Bs3KbdRead)
#define Bs3KbdWait                              BS3_CMN_MANGLER(Bs3KbdWait)
#define Bs3KbdWrite                             BS3_CMN_MANGLER(Bs3KbdWrite)
#define Bs3MemAlloc                             BS3_CMN_MANGLER(Bs3MemAlloc)
#define Bs3MemAllocZ                            BS3_CMN_MANGLER(Bs3MemAllocZ)
#define Bs3MemCpy                               BS3_CMN_MANGLER(Bs3MemCpy)
#define Bs3MemFree                              BS3_CMN_MANGLER(Bs3MemFree)
#define Bs3MemMove                              BS3_CMN_MANGLER(Bs3MemMove)
#define Bs3MemPCpy                              BS3_CMN_MANGLER(Bs3MemPCpy)
#define Bs3MemZero                              BS3_CMN_MANGLER(Bs3MemZero)
#define Bs3PagingInitRootForLM                  BS3_CMN_MANGLER(Bs3PagingInitRootForLM)
#define Bs3PagingInitRootForPAE                 BS3_CMN_MANGLER(Bs3PagingInitRootForPAE)
#define Bs3PagingInitRootForPP                  BS3_CMN_MANGLER(Bs3PagingInitRootForPP)
#define Bs3PagingProtect                        BS3_CMN_MANGLER(Bs3PagingProtect)
#define Bs3PagingProtectPtr                     BS3_CMN_MANGLER(Bs3PagingProtectPtr)
#define Bs3Panic                                BS3_CMN_MANGLER(Bs3Panic)
#define Bs3PicMaskAll                           BS3_CMN_MANGLER(Bs3PicMaskAll)
#define Bs3PrintChr                             BS3_CMN_MANGLER(Bs3PrintChr)
#define Bs3Printf                               BS3_CMN_MANGLER(Bs3Printf)
#define Bs3PrintfV                              BS3_CMN_MANGLER(Bs3PrintfV)
#define Bs3PrintStr                             BS3_CMN_MANGLER(Bs3PrintStr)
#define Bs3PrintStrN                            BS3_CMN_MANGLER(Bs3PrintStrN)
#define Bs3PrintU32                             BS3_CMN_MANGLER(Bs3PrintU32)
#define Bs3PrintX32                             BS3_CMN_MANGLER(Bs3PrintX32)
#define Bs3RegCtxConvertToRingX                 BS3_CMN_MANGLER(Bs3RegCtxConvertToRingX)
#define Bs3RegCtxPrint                          BS3_CMN_MANGLER(Bs3RegCtxPrint)
#define Bs3RegCtxRestore                        BS3_CMN_MANGLER(Bs3RegCtxRestore)
#define Bs3RegCtxSave                           BS3_CMN_MANGLER(Bs3RegCtxSave)
#define Bs3SelFar32ToFlat32                     BS3_CMN_MANGLER(Bs3SelFar32ToFlat32)
#define Bs3SelProtFar32ToFlat32                 BS3_CMN_MANGLER(Bs3SelProtFar32ToFlat32)
#define Bs3Shutdown                             BS3_CMN_MANGLER(Bs3Shutdown)
#define Bs3SlabAlloc                            BS3_CMN_MANGLER(Bs3SlabAlloc)
#define Bs3SlabAllocEx                          BS3_CMN_MANGLER(Bs3SlabAllocEx)
#define Bs3SlabFree                             BS3_CMN_MANGLER(Bs3SlabFree)
#define Bs3SlabInit                             BS3_CMN_MANGLER(Bs3SlabInit)
#define Bs3SlabListAdd                          BS3_CMN_MANGLER(Bs3SlabListAdd)
#define Bs3SlabListAlloc                        BS3_CMN_MANGLER(Bs3SlabListAlloc)
#define Bs3SlabListAllocEx                      BS3_CMN_MANGLER(Bs3SlabListAllocEx)
#define Bs3SlabListFree                         BS3_CMN_MANGLER(Bs3SlabListFree)
#define Bs3SlabListInit                         BS3_CMN_MANGLER(Bs3SlabListInit)
#define Bs3StrCpy                               BS3_CMN_MANGLER(Bs3StrCpy)
#define Bs3StrFormatV                           BS3_CMN_MANGLER(Bs3StrFormatV)
#define Bs3StrLen                               BS3_CMN_MANGLER(Bs3StrLen)
#define Bs3StrNLen                              BS3_CMN_MANGLER(Bs3StrNLen)
#define Bs3StrPrintf                            BS3_CMN_MANGLER(Bs3StrPrintf)
#define Bs3StrPrintfV                           BS3_CMN_MANGLER(Bs3StrPrintfV)
#define Bs3TestCheckRegCtxEx                    BS3_CMN_MANGLER(Bs3TestCheckRegCtxEx)
#define Bs3TestFailed                           BS3_CMN_MANGLER(Bs3TestFailed)
#define Bs3TestFailedF                          BS3_CMN_MANGLER(Bs3TestFailedF)
#define Bs3TestFailedV                          BS3_CMN_MANGLER(Bs3TestFailedV)
#define Bs3TestInit                             BS3_CMN_MANGLER(Bs3TestInit)
#define Bs3TestPrintf                           BS3_CMN_MANGLER(Bs3TestPrintf)
#define Bs3TestPrintfV                          BS3_CMN_MANGLER(Bs3TestPrintfV)
#define Bs3TestSkipped                          BS3_CMN_MANGLER(Bs3TestSkipped)
#define Bs3TestSkippedF                         BS3_CMN_MANGLER(Bs3TestSkippedF)
#define Bs3TestSkippedV                         BS3_CMN_MANGLER(Bs3TestSkippedV)
#define Bs3TestSub                              BS3_CMN_MANGLER(Bs3TestSub)
#define Bs3TestSubDone                          BS3_CMN_MANGLER(Bs3TestSubDone)
#define Bs3TestSubErrorCount                    BS3_CMN_MANGLER(Bs3TestSubErrorCount)
#define Bs3TestSubF                             BS3_CMN_MANGLER(Bs3TestSubF)
#define Bs3TestSubV                             BS3_CMN_MANGLER(Bs3TestSubV)
#define Bs3TestTerm                             BS3_CMN_MANGLER(Bs3TestTerm)
#define Bs3Trap16Init                           BS3_CMN_MANGLER(Bs3Trap16Init)
#define Bs3Trap16InitEx                         BS3_CMN_MANGLER(Bs3Trap16InitEx)
#define Bs3Trap16SetGate                        BS3_CMN_MANGLER(Bs3Trap16SetGate)
#define Bs3Trap32Init                           BS3_CMN_MANGLER(Bs3Trap32Init)
#define Bs3Trap32SetGate                        BS3_CMN_MANGLER(Bs3Trap32SetGate)
#define Bs3Trap64Init                           BS3_CMN_MANGLER(Bs3Trap64Init)
#define Bs3Trap64SetGate                        BS3_CMN_MANGLER(Bs3Trap64SetGate)
#define Bs3TrapDefaultHandler                   BS3_CMN_MANGLER(Bs3TrapDefaultHandler)
#define Bs3TrapPrintFrame                       BS3_CMN_MANGLER(Bs3TrapPrintFrame)
#define Bs3TrapRmV86SetGate                     BS3_CMN_MANGLER(Bs3TrapRmV86SetGate)
#define Bs3TrapSetHandler                       BS3_CMN_MANGLER(Bs3TrapSetHandler)
#define Bs3TrapSetJmp                           BS3_CMN_MANGLER(Bs3TrapSetJmp)
#define Bs3TrapSetJmpAndRestore                 BS3_CMN_MANGLER(Bs3TrapSetJmpAndRestore)
#define Bs3TrapUnsetJmp                         BS3_CMN_MANGLER(Bs3TrapUnsetJmp)

