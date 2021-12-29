/* $Id$ */
/** @file
 * UnattendedOs2Installer implementation.
 */

/*
 * Copyright (C) 2006-2021 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_MAIN_UNATTENDED
#include "LoggingNew.h"
#include "VirtualBoxBase.h"
#include "VirtualBoxErrorInfoImpl.h"
#include "AutoCaller.h"
#include <VBox/com/ErrorInfo.h>

#include "UnattendedImpl.h"
#include "UnattendedInstaller.h"
#include "UnattendedScript.h"

#include <VBox/err.h>
#include <iprt/ctype.h>
#include <iprt/fsisomaker.h>
#include <iprt/fsvfs.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/vfs.h>
#ifdef RT_OS_SOLARIS
# undef ES /* Workaround for someone dragging the namespace pollutor sys/regset.h. Sigh. */
#endif
#include <iprt/formats/fat.h>
#include <iprt/cpp/path.h>


using namespace std;



UnattendedOs2Installer::UnattendedOs2Installer(Unattended *pParent, Utf8Str const &rStrHints)
    : UnattendedInstaller(pParent,
                          "os2_response_files.rsp", "os2_cid_install.cmd",
                          "OS2.RSP",                "VBOXCID.CMD",
                          DeviceType_Floppy)
{
    Assert(!isOriginalIsoNeeded());
    Assert(isAuxiliaryFloppyNeeded());
    Assert(isAuxiliaryIsoIsVISO());
    Assert(bootFromAuxiliaryIso());
    mStrAuxiliaryInstallDir = "S:\\";

    /* Extract the info from the hints variable: */
    RTCList<RTCString, RTCString *> Pairs = rStrHints.split(" ");
    size_t i = Pairs.size();
    Assert(i > 0);
    while (i -- > 0)
    {
        RTCString const rStr = Pairs[i];
        if (rStr.startsWith("OS2SE20.SRC="))
            mStrOs2Images = rStr.substr(sizeof("OS2SE20.SRC=") - 1);
        else
            AssertMsgFailed(("Unknown hint: %s\n", rStr.c_str()));
    }
}


HRESULT UnattendedOs2Installer::replaceAuxFloppyImageBootSector(RTVFSFILE hVfsFile) RT_NOEXCEPT
{
    /*
     * Find the bootsector.  Because the ArcaOS ISOs doesn't contain any floppy
     * images, we cannot just lift it off one of those.  Instead we'll locate it
     * in the SYSINSTX.COM utility, i.e. the tool which installs it onto floppies
     * and harddisks.  The SYSINSTX.COM utility is a NE executable and we don't
     * have issues with compressed pages like with LX images.
     *
     * The utility seems always to be located on disk 0.
     */
    RTVFS   hVfsOrgIso;
    HRESULT hrc = openInstallIsoImage(&hVfsOrgIso);
    if (SUCCEEDED(hrc))
    {
        char szPath[256];
        int vrc = RTPathJoin(szPath, sizeof(szPath), mStrOs2Images.c_str(), "DISK_0/SYSINSTX.COM");
        if (RT_SUCCESS(vrc))
        {
            RTVFSFILE hVfsSysInstX;
            vrc = RTVfsFileOpen(hVfsOrgIso, szPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, &hVfsSysInstX);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Scan the image looking for a 512 block ending with a DOS signature
                 * and starting with a three byte jump followed by an OEM name string.
                 */
                uint8_t *pbBootSector = NULL;
                RTFOFF   off          = 0;
                bool     fEof         = false;
                uint8_t  abBuf[_8K]   = {0};
                do
                {
                    /* Read the next chunk. */
                    memmove(abBuf, &abBuf[sizeof(abBuf) - 512], 512); /* Move up the last 512 (all zero the first time around). */
                    size_t cbRead = 0;
                    vrc = RTVfsFileReadAt(hVfsSysInstX, off, &abBuf[512], sizeof(abBuf) - 512, &cbRead);
                    if (RT_FAILURE(vrc))
                        break;
                    fEof = cbRead != sizeof(abBuf) - 512;
                    off += cbRead;

                    /* Scan it. */
                    size_t   cbLeft = sizeof(abBuf);
                    uint8_t *pbCur  = abBuf;
                    while (cbLeft >= 512)
                    {
                        /* Look for the DOS signature (0x55 0xaa) at the end of the sector: */
                        uint8_t *pbHit = (uint8_t *)memchr(pbCur + 510, 0x55, cbLeft - 510 - 1);
                        if (!pbHit)
                            break;
                        if (pbHit[1] == 0xaa)
                        {
                            uint8_t *pbStart = pbHit - 510;
                            if (   pbStart[0] == 0xeb        /* JMP imm8 */
                                && pbStart[1] >= 3 + 8 + sizeof(FATEBPB) - 2 /* must jump after the FATEBPB */
                                && RT_C_IS_ALNUM(pbStart[3]) /* ASSUME OEM string starts with two letters (typically 'IBM x.y')*/
                                && RT_C_IS_ALNUM(pbStart[4]))
                            {
                                FATEBPB *pBpb = (FATEBPB *)&pbStart[3 + 8];
                                if (   pBpb->bExtSignature == FATEBPB_SIGNATURE
                                    && (   memcmp(pBpb->achType, "FAT     ", sizeof(pBpb->achType)) == 0
                                        || memcmp(pBpb->achType, FATEBPB_TYPE_FAT12, sizeof(pBpb->achType)) == 0))
                                {
                                    pbBootSector = pbStart;
                                    break;
                                }
                            }
                        }

                        /* skip */
                        pbCur  = pbHit - 510 + 1;
                        cbLeft = (uintptr_t)&abBuf[sizeof(abBuf)] - (uintptr_t)pbCur;
                    }
                } while (!fEof);

                if (pbBootSector)
                {
                    if (pbBootSector != abBuf)
                        pbBootSector = (uint8_t *)memmove(abBuf, pbBootSector, 512);

                    /*
                     * We've now got a bootsector.  So, we need to copy the EBPB
                     * from the destination image before replacing it.
                     */
                    vrc = RTVfsFileReadAt(hVfsFile, 0, &abBuf[512], 512, NULL);
                    if (RT_SUCCESS(vrc))
                    {
                        memcpy(&pbBootSector[3 + 8], &abBuf[512 + 3 + 8], sizeof(FATEBPB));

                        /*
                         * Write it.
                         */
                        vrc = RTVfsFileWriteAt(hVfsFile, 0, pbBootSector, 512, NULL);
                        if (RT_SUCCESS(vrc))
                        {
                            LogFlowFunc(("Successfully installed new bootsector\n"));
                            hrc = S_OK;
                        }
                        else
                            hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to write bootsector: %Rrc"), vrc);
                    }
                    else
                        hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to read bootsector: %Rrc"), vrc);
                }
                else if (RT_FAILURE(vrc))
                    hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error reading SYSINSTX.COM: %Rrc"), vrc);
                else
                    hrc = mpParent->setErrorBoth(E_FAIL, VERR_NOT_FOUND,
                                                 tr("Unable to locate bootsector template in SYSINSTX.COM"));
                RTVfsFileRelease(hVfsSysInstX);
            }
            else
                hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to open SYSINSTX.COM: %Rrc"), vrc);
        }
        else
            hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to construct SYSINSTX.COM path"));
        RTVfsRelease(hVfsOrgIso);
    }
    return hrc;

}

HRESULT UnattendedOs2Installer::newAuxFloppyImage(const char *pszFilename, bool fOverwrite, PRTVFSFILE phVfsFile)
{
    /*
     * Open the image file.
     */
    HRESULT     hrc;
    RTVFSFILE   hVfsFile;
    uint64_t    fOpen = RTFILE_O_READWRITE | RTFILE_O_DENY_ALL | (0660 << RTFILE_O_CREATE_MODE_SHIFT);
    if (fOverwrite)
        fOpen |= RTFILE_O_CREATE_REPLACE;
    else
        fOpen |= RTFILE_O_OPEN;
    int vrc = RTVfsFileOpenNormal(pszFilename, fOpen, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Format it.
         */
        vrc = RTFsFatVolFormat288(hVfsFile, false /*fQuick*/);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Now we install the OS/2 boot sector on it.
             */
            hrc = replaceAuxFloppyImageBootSector(hVfsFile);
            if (SUCCEEDED(hrc))
            {
                *phVfsFile = hVfsFile;
                LogRelFlow(("UnattendedInstaller::newAuxFloppyImage: created and formatted  '%s'\n", pszFilename));
                return S_OK;
            }
        }
        else
            hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("Failed to format floppy image '%s': %Rrc"), pszFilename, vrc);
        RTVfsFileRelease(hVfsFile);
        RTFileDelete(pszFilename);
    }
    else
        hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("Failed to create floppy image '%s': %Rrc"), pszFilename, vrc);
    return hrc;
}


HRESULT UnattendedOs2Installer::splitResponseFile() RT_NOEXCEPT
{
    if (mVecSplitFiles.size() == 0)
    {
#if 0
        Utf8Str strResponseFile;
        int vrc = strResponseFile.assignNoThrow(mpParent->i_getAuxiliaryBasePath());
        if (RT_SUCCESS(vrc))
            vrc = strResponseFile.appendNoThrow(mMainScript.getDefaultFilename());
        if (RT_SUCCESS(vrc))
            return splitFile(strResponseFile.c_str(), mVecSplitFiles);
        return mpParent->setErrorVrc(vrc);
#else
        return splitFile(&mMainScript, mVecSplitFiles);
#endif
    }
    return S_OK;
}

/**
 * OS/2 code pattern.
 */
typedef struct OS2CODEPATTERN
{
    /** The code pattern. */
    uint8_t const *pbPattern;
    /** The mask to apply when using the pattern (ignore 0x00 bytes, compare 0xff
     *  bytes). */
    uint8_t const *pbMask;
    /** The pattern size.  */
    size_t         cb;
    /** User info \#1. */
    uintptr_t      uUser1;
    /** User info \#2. */
    uint32_t       uUser2;
    /** User info \#3. */
    uint32_t       uUser3;
    /** User info \#4. */
    uint32_t       uUser4;
    /** User info \#5. */
    uint32_t       uUser5;
} OS2CODEPATTERN;
/** Pointer to an OS/2 code pattern.   */
typedef OS2CODEPATTERN const *PCOS2CODEPATTERN;


/**
 * Search @a pbCode for the code patterns in @a paPatterns.
 *
 * @returns pointer within @a pbCode to matching code, NULL if no match.
 */
static uint8_t *findCodePattern(PCOS2CODEPATTERN paPatterns, size_t cPatterns, uint8_t *pbCode, size_t cbCode,
                                PCOS2CODEPATTERN *ppMatch)
{
    for (size_t i = 0; i < cPatterns; i++)
    {
        size_t const   cbPattern = paPatterns[i].cb;
        uint8_t const *pbPattern = paPatterns[i].pbPattern;
        uint8_t const *pbMask    = paPatterns[i].pbMask;
        Assert(pbMask[0] == 0xff); /* ASSUME the we can use the first byte with memchr. */
        uint8_t const  bFirst    = *pbPattern;
        size_t         off       = 0;
        while (off + cbPattern <= cbCode)
        {
            uint8_t *pbHit = (uint8_t *)memchr(&pbCode[off], bFirst, cbCode - off - cbPattern + 1);
            if (!pbHit)
                break;

            size_t offPattern = 1;
            while (   offPattern < cbPattern
                   && (pbPattern[offPattern] & pbMask[offPattern]) == (pbHit[offPattern] & pbMask[offPattern]))
                offPattern++;
            if (offPattern == cbPattern)
            {
                *ppMatch = &paPatterns[i];
                return pbHit;
            }

            /* next */
            off++;
        }
    }
    return NULL;
}


/**
 * Patcher callback for OS2LDR.
 *
 * There are one or two delay calibration loops here that doesn't work well on
 * fast CPUs. Typically ends up with division by chainsaw, which in a BIOS
 * context means an unending loop as the BIOS \#DE handler doesn't do much.
 *
 * The patching is simplictic, in that it just returns a constant value.  We
 * could rewrite this to use RDTSC and some secret MSR/whatever for converting
 * that to a decent loop count.
 */
/*static*/
int UnattendedOs2Installer::patchOs2Ldr(uint8_t *pbFile, size_t cbFile, const char *pszFilename, UnattendedOs2Installer *pThis)
{
    RT_NOREF(pThis, pszFilename);

    /*
     * This first variant is from ACP2:
     *
     * VBoxDbg> r
     * eax=00001000 ebx=00010000 ecx=56d8ffd5 edx=178b0000 esi=00000000 edi=0000b750
     * eip=0000847a esp=0000cfe8 ebp=00000000 iopl=0 nv up ei pl zr na po nc
     * cs=2000 ds=2000 es=2000 fs=0000 gs=0000 ss=2000               eflags=00000246
     * 2000:0000847a f7 fb                   idiv bx
     * VBoxDbg> ucfg 2000:840a
     *
     * This is a little annoying because it stores the result in a global variable,
     * so we cannot just do an early return, instead we have to have to jump to the
     * end of the function so it can be stored correctly.
     */
    static uint8_t const s_abVariant1[] =
    {
        /*2000:840a*/ 0x60,                 /* pushaw             */
        /*2000:840b*/ 0x1e,                 /* push DS            */
        /*2000:840c*/ 0x0e,                 /* push CS            */
        /*2000:840d*/ 0x1f,                 /* pop DS             */
        /*2000:840e*/ 0x9c,                 /* pushfw             */
        /*2000:840f*/ 0xfa,                 /* cli                */
        /*2000:8410*/ 0xb0, 0x34,           /* mov AL, 034h       */
        /*2000:8412*/ 0xe6, 0x43,           /* out 043h, AL       */
        /*2000:8414*/ 0xe8, 0x75, 0xfc,     /* call 0808ch        */
        /*2000:8417*/ 0x32, 0xc0,           /* xor al, al         */
        /*2000:8419*/ 0xe6, 0x40,           /* out 040h, AL       */
        /*2000:841b*/ 0xe8, 0x6e, 0xfc,     /* call 0808ch        */
        /*2000:841e*/ 0xe6, 0x40,           /* out 040h, AL       */
        /*2000:8420*/ 0xe8, 0x69, 0xfc,     /* call 0808ch        */
        /*2000:8423*/ 0xb0, 0x00,           /* mov AL, 000h       */
        /*2000:8425*/ 0xe6, 0x43,           /* out 043h, AL       */
        /*2000:8427*/ 0xe8, 0x62, 0xfc,     /* call 0808ch        */
        /*2000:842a*/ 0xe4, 0x40,           /* in AL, 040h        */
        /*2000:842c*/ 0xe8, 0x5d, 0xfc,     /* call 0808ch        */
        /*2000:842f*/ 0x8a, 0xd8,           /* mov bl, al         */
        /*2000:8431*/ 0xe4, 0x40,           /* in AL, 040h        */
        /*2000:8433*/ 0x8a, 0xf8,           /* mov bh, al         */
        /*2000:8435*/ 0xb0, 0x00,           /* mov AL, 000h       */
        /*2000:8437*/ 0xe6, 0x43,           /* out 043h, AL       */
        /*2000:8439*/ 0xe8, 0x50, 0xfc,     /* call 0808ch        */
        /*2000:843c*/ 0xe4, 0x40,           /* in AL, 040h        */
        /*2000:843e*/ 0xe8, 0x4b, 0xfc,     /* call 0808ch        */
        /*2000:8441*/ 0x8a, 0xc8,           /* mov cl, al         */
        /*2000:8443*/ 0xe4, 0x40,           /* in AL, 040h        */
        /*2000:8445*/ 0x8a, 0xe8,           /* mov ch, al         */
        /*2000:8447*/ 0xbe, 0x00, 0x10,     /* mov si, 01000h     */
        /*2000:844a*/ 0x87, 0xdb,           /* xchg bx, bx        */
        /*2000:844c*/ 0x4e,                 /* dec si             */
        /*2000:844d*/ 0x75, 0xfd,           /* jne -003h (0844ch) */
        /*2000:844f*/ 0xb0, 0x00,           /* mov AL, 000h       */
        /*2000:8451*/ 0xe6, 0x43,           /* out 043h, AL       */
        /*2000:8453*/ 0xe8, 0x36, 0xfc,     /* call 0808ch        */
        /*2000:8456*/ 0xe4, 0x40,           /* in AL, 040h        */
        /*2000:8458*/ 0xe8, 0x31, 0xfc,     /* call 0808ch        */
        /*2000:845b*/ 0x8a, 0xd0,           /* mov dl, al         */
        /*2000:845d*/ 0xe4, 0x40,           /* in AL, 040h        */
        /*2000:845f*/ 0x8a, 0xf0,           /* mov dh, al         */
        /*2000:8461*/ 0x9d,                 /* popfw              */
        /*2000:8462*/ 0x2b, 0xd9,           /* sub bx, cx         */
        /*2000:8464*/ 0x2b, 0xca,           /* sub cx, dx         */
        /*2000:8466*/ 0x2b, 0xcb,           /* sub cx, bx         */
        /*2000:8468*/ 0x87, 0xca,           /* xchg dx, cx        */
        /*2000:846a*/ 0xb8, 0x28, 0x00,     /* mov ax, 00028h     */
        /*2000:846d*/ 0xf7, 0xea,           /* imul dx            */
        /*2000:846f*/ 0xbb, 0x18, 0x00,     /* mov bx, 00018h     */
        /*2000:8472*/ 0xf7, 0xfb,           /* idiv bx            */
        /*2000:8474*/ 0x33, 0xd2,           /* xor dx, dx         */
        /*2000:8476*/ 0xbb, 0x00, 0x10,     /* mov bx, 01000h     */
        /*2000:8479*/ 0x93,                 /* xchg bx, ax        */
        /*2000:847a*/ 0xf7, 0xfb,           /* idiv bx            */
        /*2000:847c*/ 0x0b, 0xd2,           /* or dx, dx          */
        /*2000:847e*/ 0x74, 0x01,           /* je +001h (08481h)  */
        /*2000:8480*/ 0x40,                 /* inc ax             */
        /*2000:8481*/ 0x40,                 /* inc ax             */
        /*2000:8482*/ 0xa3, 0x4d, 0xac,     /* mov word [0ac4dh], ax */
        /*2000:8485*/ 0x1f,                 /* pop DS             */
        /*2000:8486*/ 0x61,                 /* popaw              */
        /*2000:8487*/ 0xc3,                 /* retn               */
    };
    static uint8_t const s_abVariant1Mask[] =
    {
        /*2000:840a*/ 0xff,                 /* pushaw             */
        /*2000:840b*/ 0xff,                 /* push DS            */
        /*2000:840c*/ 0xff,                 /* push CS            */
        /*2000:840d*/ 0xff,                 /* pop DS             */
        /*2000:840e*/ 0xff,                 /* pushfw             */
        /*2000:840f*/ 0xff,                 /* cli                */
        /*2000:8410*/ 0xff, 0xff,           /* mov AL, 034h       */
        /*2000:8412*/ 0xff, 0xff,           /* out 043h, AL       */
        /*2000:8414*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:8417*/ 0xff, 0xff,           /* xor al, al         */
        /*2000:8419*/ 0xff, 0xff,           /* out 040h, AL       */
        /*2000:841b*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:841e*/ 0xff, 0xff,           /* out 040h, AL       */
        /*2000:8420*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:8423*/ 0xff, 0xff,           /* mov AL, 000h       */
        /*2000:8425*/ 0xff, 0xff,           /* out 043h, AL       */
        /*2000:8427*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:842a*/ 0xff, 0xff,           /* in AL, 040h        */
        /*2000:842c*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:842f*/ 0xff, 0xff,           /* mov bl, al         */
        /*2000:8431*/ 0xff, 0xff,           /* in AL, 040h        */
        /*2000:8433*/ 0xff, 0xff,           /* mov bh, al         */
        /*2000:8435*/ 0xff, 0xff,           /* mov AL, 000h       */
        /*2000:8437*/ 0xff, 0xff,           /* out 043h, AL       */
        /*2000:8439*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:843c*/ 0xff, 0x40,           /* in AL, 040h        */
        /*2000:843e*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:8441*/ 0xff, 0xff,           /* mov cl, al         */
        /*2000:8443*/ 0xff, 0xff,           /* in AL, 040h        */
        /*2000:8445*/ 0xff, 0xff,           /* mov ch, al         */
        /*2000:8447*/ 0xff, 0x00, 0x00,     /* mov si, 01000h     - ignore loop count */
        /*2000:844a*/ 0xff, 0xff,           /* xchg bx, bx        */
        /*2000:844c*/ 0xff,                 /* dec si             */
        /*2000:844d*/ 0xff, 0xfd,           /* jne -003h (0844ch) */
        /*2000:844f*/ 0xff, 0xff,           /* mov AL, 000h       */
        /*2000:8451*/ 0xff, 0xff,           /* out 043h, AL       */
        /*2000:8453*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:8456*/ 0xff, 0xff,           /* in AL, 040h        */
        /*2000:8458*/ 0xff, 0x00, 0x00,     /* call 0808ch        - ignore offset */
        /*2000:845b*/ 0xff, 0xff,           /* mov dl, al         */
        /*2000:845d*/ 0xff, 0xff,           /* in AL, 040h        */
        /*2000:845f*/ 0xff, 0xff,           /* mov dh, al         */
        /*2000:8461*/ 0xff,                 /* popfw              */
        /*2000:8462*/ 0xff, 0xff,           /* sub bx, cx         */
        /*2000:8464*/ 0xff, 0xff,           /* sub cx, dx         */
        /*2000:8466*/ 0xff, 0xff,           /* sub cx, bx         */
        /*2000:8468*/ 0xff, 0xff,           /* xchg dx, cx        */
        /*2000:846a*/ 0xff, 0xff, 0xff,     /* mov ax, 00028h     */
        /*2000:846d*/ 0xff, 0xff,           /* imul dx            */
        /*2000:846f*/ 0xff, 0xff, 0xff,     /* mov bx, 00018h     */
        /*2000:8472*/ 0xff, 0xff,           /* idiv bx            */
        /*2000:8474*/ 0xff, 0xff,           /* xor dx, dx         */
        /*2000:8476*/ 0xff, 0x00, 0x00,     /* mov bx, 01000h     - ignore loop count */
        /*2000:8479*/ 0xff,                 /* xchg bx, ax        */
        /*2000:847a*/ 0xff, 0xff,           /* idiv bx            */
        /*2000:847c*/ 0xff, 0xff,           /* or dx, dx          */
        /*2000:847e*/ 0xff, 0xff,           /* je +001h (08481h)  */
        /*2000:8480*/ 0xff,                 /* inc ax             */
        /*2000:8481*/ 0xff,                 /* inc ax             */
        /*2000:8482*/ 0xff, 0x00, 0x00,     /* mov word [0ac4dh], ax */
        /*2000:8485*/ 0xff,                 /* pop DS             */
        /*2000:8486*/ 0xff,                 /* popaw              */
        /*2000:8487*/ 0xff,                 /* retn               */
    };
    AssertCompile(sizeof(s_abVariant1Mask) == sizeof(s_abVariant1));

    /* uUser1 = off to start injecting code; uUser2 = jump target offset from start of pattern */
    static const OS2CODEPATTERN s_aPatterns[] =
    {
        {  s_abVariant1, s_abVariant1Mask, sizeof(s_abVariant1Mask), 0x840e - 0x840a, 0x8482 - 0x840a, 0, 0, 0 },
    };

    PCOS2CODEPATTERN pPattern;
    uint8_t *pbHit = findCodePattern(&s_aPatterns[0], RT_ELEMENTS(s_aPatterns), pbFile, cbFile, &pPattern);
    if (pPattern)
    {
        uint8_t *pbJmpTarget = &pbHit[pPattern->uUser2];
        uint8_t *pbPatch = &pbHit[pPattern->uUser1];
        *pbPatch++ = 0xb8;  /* mov ax, 01000h */
        *pbPatch++ = 0x00;
        *pbPatch++ = 0x10;
#if 0
        *pbPatch++ = 0xfa; /* cli */
        *pbPatch++ = 0xf4; /* hlt */
#endif
        uint16_t offRel16 = (uint16_t)(pbJmpTarget - &pbPatch[3]);
        *pbPatch++ = 0xe9;  /* jmp rel16 */
        *pbPatch++ = (uint8_t)offRel16;
        *pbPatch++ = (uint8_t)(offRel16 >> 8);
        *pbPatch++ = 0xcc;
        *pbPatch++ = 0xcc;
    }
    else
        LogRelFunc(("No patch pattern match!\n"));

    return VINF_SUCCESS;
}

HRESULT UnattendedOs2Installer::copyFilesToAuxFloppyImage(RTVFS hVfs)
{
    /*
     * Make sure we've split the files already.
     */
    HRESULT hrc = splitResponseFile();
    if (FAILED(hrc))
        return hrc;

    /*
     * We need to copy over the files needed to boot OS/2.
     */
    static struct
    {
        bool        fMandatory;
        const char *apszNames[2]; /**< Will always copy it over using the first name. */
        const char *apszDisks[3];
        const char *pszMinVer;
        const char *pszMaxVer;
        int       (*pfnPatcher)(uint8_t *pbFile, size_t cbFile, const char *pszFilename, UnattendedOs2Installer *pThis);
    } const s_aFiles[] =
    {
        { true, { "OS2BOOT",      NULL          }, { "DISK_0", NULL,     NULL }, "2.1",  NULL, NULL }, /* 2.0 did not have OS2BOOT */
        { true, { "OS2LDR",       NULL          }, { "DISK_0", NULL,     NULL }, NULL,   NULL, patchOs2Ldr },
        { true, { "OS2LDR.MSG",   NULL          }, { "DISK_0", NULL,     NULL }, NULL,   NULL, NULL },
        { true, { "OS2KRNL",      "OS2KRNLI"    }, { "DISK_0", NULL,     NULL }, NULL,   NULL, NULL }, /* OS2KRNLI seems to trigger question for 2nd floppy */
        { true, { "OS2DUMP",      NULL          }, { "DISK_0", NULL,     NULL }, NULL,   NULL, NULL },

        { true, { "ANSICALL.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "BKSCALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "BMSCALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "BVHINIT.DLL",  NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "BVSCALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "CDFS.IFS",     NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "CLOCK01.SYS",  NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "COUNT437.SYS", "COUNTRY.SYS" }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "DOS.SYS",      NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "DOSCALL1.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "IBM1FLPY.ADD", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "IBM1S506.ADD", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "IBMIDECD.FLT", NULL          }, { "DISK_1", "DISK_2", NULL }, "4.0",  NULL, NULL }, /* not in 2.1 & Warp3  */
        { true, { "IBMKBD.SYS", "KBD01.SYS"/*?*/}, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
#if 1 /* Sometimes takes forever. (Bad IODelay count? Fixed by OS2LDR patching?) Removing seems to cause testcfg.sys to crash. */
        { true, { "ISAPNP.SNP",   NULL          }, { "DISK_1", "DISK_2", NULL }, "4.0",  NULL, NULL }, /* not in 2.1 */
#endif
        { true, { "KBDBASE.SYS",  NULL          }, { "DISK_1", "DISK_2", NULL }, "3.0",  NULL, NULL }, /* not in 2.1 */
        { true, { "KBDCALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "KEYBOARD.DCP", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "MOUCALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "MSG.DLL",      NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "NAMPIPES.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "NLS.DLL",      NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "OS2CDROM.DMD", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "OS2CHAR.DLL",  NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "OS2DASD.DMD",  NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "OS2LVM.DMD",   NULL          }, { "DISK_1", "DISK_2", NULL }, "4.5",  NULL, NULL },
        { true, { "OS2VER",       NULL          }, { "DISK_0", NULL,     NULL }, NULL,   NULL, NULL },
        { true, { "PNP.SYS",      NULL          }, { "DISK_1", "DISK_2", NULL }, "4.0",  NULL, NULL },
        { true, { "QUECALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "RESOURCE.SYS", NULL          }, { "DISK_1", "DISK_2", NULL }, "3.0",  NULL, NULL }, /* not in 2.1*/
        { true, { "SCREEN01.SYS", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "SESMGR.DLL",   NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "TESTCFG.SYS",  NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "VIO437.DCP",   "VTBL850.DCP" }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
        { true, { "VIOCALLS.DLL", NULL          }, { "DISK_1", "DISK_2", NULL }, NULL,   NULL, NULL },
    };


    RTVFS hVfsOrgIso;
    hrc = openInstallIsoImage(&hVfsOrgIso);
    if (SUCCEEDED(hrc))
    {
        for (size_t i = 0; i < RT_ELEMENTS(s_aFiles); i++)
        {
            bool fCopied = false;
            for (size_t iDisk = 0; iDisk < RT_ELEMENTS(s_aFiles[i].apszDisks) && s_aFiles[i].apszDisks[iDisk] && !fCopied; iDisk++)
            {
                for (size_t iName = 0; iName < RT_ELEMENTS(s_aFiles[i].apszNames) && s_aFiles[i].apszNames[iName]; iName++)
                {
                    char szPath[256];
                    int vrc = RTPathJoin(szPath, sizeof(szPath), mStrOs2Images.c_str(), s_aFiles[i].apszDisks[iDisk]);
                    if (RT_SUCCESS(vrc))
                        vrc = RTPathAppend(szPath, sizeof(szPath), s_aFiles[i].apszNames[iName]);
                    AssertRCBreakStmt(vrc, hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("RTPathJoin/Append failed for %s: %Rrc"),
                                                                        s_aFiles[i].apszNames[iName], vrc));
                    RTVFSFILE hVfsSrc;
                    vrc = RTVfsFileOpen(hVfsOrgIso, szPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, &hVfsSrc);
                    if (RT_SUCCESS(vrc))
                    {
                        RTVFSFILE hVfsDst;
                        vrc = RTVfsFileOpen(hVfs, s_aFiles[i].apszNames[0],
                                            RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE
                                            | (0755 << RTFILE_O_CREATE_MODE_SHIFT), &hVfsDst);
                        if (RT_SUCCESS(vrc))
                        {
                            if (!s_aFiles[i].pfnPatcher)
                            {
                                /*
                                 * Not patching this file, so just pump it thru and close it.
                                 */
                                RTVFSIOSTREAM hVfsIosSrc = RTVfsFileToIoStream(hVfsSrc);
                                RTVFSIOSTREAM hVfsIosDst = RTVfsFileToIoStream(hVfsDst);
                                vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, 0);
                                RTVfsIoStrmRelease(hVfsIosDst);
                                RTVfsIoStrmRelease(hVfsIosSrc);
                                if (RT_FAILURE(vrc))
                                    hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                                                 tr("Failed to write %s to the floppy: %Rrc"),
                                                                 s_aFiles[i].apszNames, vrc);
                            }
                            else
                            {
                                /*
                                 * Read the file into memory, do the patching and writed
                                 * the patched content to the floppy.
                                 */
                                uint64_t cbFile = 0;
                                vrc = RTVfsFileQuerySize(hVfsSrc, &cbFile);
                                if (RT_SUCCESS(vrc) && cbFile < _32M)
                                {
                                    uint8_t *pbFile = (uint8_t *)RTMemTmpAllocZ((size_t)cbFile);
                                    if (pbFile)
                                    {
                                        vrc = RTVfsFileRead(hVfsSrc, pbFile, (size_t)cbFile, NULL);
                                        if (RT_SUCCESS(vrc))
                                        {
                                            vrc = s_aFiles[i].pfnPatcher(pbFile, (size_t)cbFile, s_aFiles[i].apszNames[0], this);
                                            if (RT_SUCCESS(vrc))
                                            {
                                                vrc = RTVfsFileWrite(hVfsDst, pbFile, (size_t)cbFile, NULL);
                                                if (RT_FAILURE(vrc))
                                                    hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                                                                 tr("Failed to write %s to the floppy: %Rrc"),
                                                                                 s_aFiles[i].apszNames, vrc);
                                            }
                                            else
                                                hrc = mpParent->setErrorBoth(E_FAIL, vrc, tr("Patcher failed for '%s': %Rrc"),
                                                                             s_aFiles[i].apszNames, vrc);
                                        }
                                        else
                                            hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                                                         tr("Error reading '%s' into memory for patching: %Rrc"),
                                                                         s_aFiles[i].apszNames, vrc);
                                        RTMemTmpFree(pbFile);
                                    }
                                    else
                                        hrc = mpParent->setError(E_OUTOFMEMORY, tr("Failed to allocate %zu bytes for '%s'"),
                                                                 (size_t)cbFile, s_aFiles[i].apszNames);
                                }
                                else if (RT_FAILURE(vrc))
                                    hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                                                 tr("Failed to query the size of '%s': %Rrc"),
                                                                 s_aFiles[i].apszNames, vrc);
                                else
                                    hrc = mpParent->setErrorBoth(E_FAIL, VERR_OUT_OF_RANGE, tr("File too big to patch: '%s'"),
                                                                 s_aFiles[i].apszNames);
                            }
                            RTVfsFileRelease(hVfsDst);
                        }
                        else
                            hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to open %s on floppy: %Rrc"),
                                                         s_aFiles[i].apszNames, vrc);

                        RTVfsFileRelease(hVfsSrc);
                        fCopied = true;
                        break;
                    }
                }
            }
            if (FAILED(hrc))
                break;
            if (!fCopied)
            {
                /** @todo do version filtering.   */
                hrc = mpParent->setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND,
                                             tr("Failed to locate '%s' needed for the install floppy"), s_aFiles[i].apszNames[0]);
                break;
            }
        }
        RTVfsRelease(hVfsOrgIso);
    }

    /*
     * In addition, we need to add a CONFIG.SYS and the startup script.
     */
    if (SUCCEEDED(hrc))
    {
        Utf8Str strSrc;
        try
        {
            strSrc = mpParent->i_getAuxiliaryBasePath();
            strSrc.append("CONFIG.SYS");
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        hrc = addFileToFloppyImage(hVfs, strSrc.c_str(), "CONFIG.SYS");
    }

    /*
     * We also want a ALTF2ON.$$$ file so we can see which drivers are loaded
     * and where it might get stuck.
     */
    if (SUCCEEDED(hrc))
    {
        RTVFSFILE hVfsFile;
        int vrc = RTVfsFileOpen(hVfs, "ALTF2ON.$$$",
                                RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE
                                | (0755 << RTFILE_O_CREATE_MODE_SHIFT), &hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            /** @todo buggy fat vfs: cannot write empty files */
            RTVfsFileWrite(hVfsFile, RT_STR_TUPLE("\r\n"), NULL);
            RTVfsFileRelease(hVfsFile);
        }
        else
            hrc = mpParent->setErrorBoth(E_FAIL, VERR_FILE_NOT_FOUND, tr("Failed to create 'ALTF2ON.$$$' on the install floppy"));
    }

    return hrc;
}

HRESULT UnattendedOs2Installer::addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                                         RTVFS hVfsOrgIso, bool fOverwrite)
{
    /*
     * Make sure we've split the files already.
     */
    HRESULT hrc = splitResponseFile();
    if (FAILED(hrc))
        return hrc;

    /*
     * Add our stuff to the vectors.
     */
    try
    {
        /* Remaster ISO. */
        rVecArgs.append() = "--no-file-mode";
        rVecArgs.append() = "--no-dir-mode";

        rVecArgs.append() = "--import-iso";
        rVecArgs.append(mpParent->i_getIsoPath());

        rVecArgs.append() = "--file-mode=0444";
        rVecArgs.append() = "--dir-mode=0555";

        /* Add the boot floppy to the ISO: */
        rVecArgs.append() = "--eltorito-new-entry";
        rVecArgs.append() = "--eltorito-add-image";
        rVecArgs.append(mStrAuxiliaryFloppyFilePath);
        rVecArgs.append() = "--eltorito-floppy-288";


        /* Add the response files and postinstall files to the ISO: */
        Utf8Str const &rStrAuxPrefix = mpParent->i_getAuxiliaryBasePath();
        size_t i = mVecSplitFiles.size();
        while (i-- > 0)
        {
            RTCString const &rStrFile = mVecSplitFiles[i];
            rVecArgs.append().assign("VBoxCID/").append(rStrFile).append('=').append(rStrAuxPrefix).append(rStrFile);
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Call parent.
     */
    return UnattendedInstaller::addFilesToAuxVisoVectors(rVecArgs, rVecFiles, hVfsOrgIso, fOverwrite);
}

/**
 * Helper for splitFile.
 */
const char *splitFileLocateSubstring(const char *pszSrc, size_t cchSrc, const char *pszSubstring, size_t cchSubstring)
{
    char const ch0 = *pszSubstring;
    while (cchSrc >= cchSubstring)
    {
        const char *pszHit0 = (const char *)memchr(pszSrc, ch0, cchSrc - cchSubstring + 1);
        if (pszHit0)
        {
            if (memcmp(pszHit0, pszSubstring, cchSubstring) == 0)
                return pszHit0;
        }
        else
            break;
        cchSrc -= (size_t)(pszHit0 - pszSrc) + 1;
        pszSrc  = pszHit0 + 1;
    }
    return NULL;
}

/**
 * Worker for splitFile().
 */
HRESULT UnattendedOs2Installer::splitFileInner(const char *pszFileToSplit, RTCList<RTCString> &rVecSplitFiles,
                                               const char *pszSrc, size_t cbLeft) RT_NOEXCEPT
{
    static const char  s_szPrefix[] = "@@VBOX_SPLITTER_";
    const char * const pszStart     = pszSrc;
    const char * const pszEnd       = &pszSrc[cbLeft];
    while (cbLeft > 0)
    {
        /*
         * Locate the next split start marker (everything before it is ignored).
         */
        const char *pszMarker = splitFileLocateSubstring(pszSrc, cbLeft, s_szPrefix, sizeof(s_szPrefix) - 1);
        if (pszMarker)
            pszMarker += sizeof(s_szPrefix) - 1;
        else
            break;
        if (strncmp(pszMarker, RT_STR_TUPLE("START[")) != 0)
            return mpParent->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                          tr("Unexpected splitter tag in '%s' at offset %p: @@VBOX_SPLITTER_%.64s"),
                                          pszFileToSplit, pszMarker - pszStart, pszMarker);
        pszMarker += sizeof("START[") - 1;
        const char *pszTail = splitFileLocateSubstring(pszMarker, (size_t)(pszEnd - pszMarker), RT_STR_TUPLE("]@@"));
        if (   !pszTail
            || pszTail - pszMarker > 64
            || memchr(pszMarker, '\\', pszTail - pszMarker)
            || memchr(pszMarker, '/', pszTail - pszMarker)
            || memchr(pszMarker, ':', pszTail - pszMarker)
           )
            return mpParent->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                          tr("Malformed splitter tag in '%s' at offset %p: @@VBOX_SPLITTER_START[%.64s"),
                                          pszFileToSplit, (size_t)(pszEnd - pszMarker), pszMarker);
        int vrc = RTStrValidateEncodingEx(pszMarker, pszTail - pszMarker, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        if (RT_FAILURE(vrc))
            return mpParent->setErrorBoth(E_FAIL, vrc,
                                          tr("Malformed splitter tag in '%s' at offset %p: @@VBOX_SPLITTER_START[%.*Rhxs"),
                                          pszFileToSplit, (size_t)(pszEnd - pszMarker), pszTail - pszMarker, pszMarker);
        const char *pszFilename;
        try
        {
            pszFilename = rVecSplitFiles.append().assign(pszMarker, pszTail - pszMarker).c_str();
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        const char *pszDocStart = pszTail + sizeof("]@@") - 1;
        while (RT_C_IS_SPACE(*pszDocStart))
            if (*pszDocStart++ == '\n')
                break;

        /* Advance. */
        pszSrc = pszDocStart;
        cbLeft = pszEnd - pszDocStart;

        /*
         * Locate the matching end marker (there cannot be any other markers inbetween).
         */
        const char * const pszDocEnd = pszMarker = splitFileLocateSubstring(pszSrc, cbLeft, s_szPrefix, sizeof(s_szPrefix) - 1);
        if (pszMarker)
            pszMarker += sizeof(s_szPrefix) - 1;
        else
            return mpParent->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                          tr("No END splitter tag for '%s' in '%s'"), pszFilename, pszFileToSplit);
        if (strncmp(pszMarker, RT_STR_TUPLE("END[")) != 0)
            return mpParent->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                          tr("Unexpected splitter tag in '%s' at offset %p: @@VBOX_SPLITTER_%.64s"),
                                          pszFileToSplit, (size_t)(pszEnd - pszMarker), pszMarker);
        pszMarker += sizeof("END[") - 1;
        size_t const cchFilename = strlen(pszFilename);
        if (   strncmp(pszMarker, pszFilename, cchFilename) != 0
            || pszMarker[cchFilename] != ']'
            || pszMarker[cchFilename + 1] != '@'
            || pszMarker[cchFilename + 2] != '@')
            return mpParent->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                          tr("Mismatching splitter tag for '%s' in '%s' at offset %p: @@VBOX_SPLITTER_END[%.64Rhxs"),
                                          pszFilename, pszFileToSplit, (size_t)(pszEnd - pszMarker), pszMarker);

        /* Advance. */
        pszSrc = pszMarker + cchFilename + sizeof("]@@") - 1;
        cbLeft = (size_t)(pszEnd - pszSrc);

        /*
         * Write out the file.
         */
        Utf8Str strDstFilename;
        vrc = strDstFilename.assignNoThrow(mpParent->i_getAuxiliaryBasePath());
        if (RT_SUCCESS(vrc))
            vrc = strDstFilename.appendNoThrow(pszFilename);
        if (RT_SUCCESS(vrc))
        {
            RTFILE hFile;
            vrc = RTFileOpen(&hFile, strDstFilename.c_str(), RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTFileWrite(hFile, pszDocStart, (size_t)(pszDocEnd - pszDocStart), NULL);
                if (RT_SUCCESS(vrc))
                    vrc = RTFileClose(hFile);
                else
                    RTFileClose(hFile);
                if (RT_FAILURE(vrc))
                    return mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error writing '%s' (split out from '%s'): %Rrc"),
                                                  strDstFilename.c_str(), pszFileToSplit, vrc);
            }
            else
                return mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                              tr("File splitter failed to open output file '%s' in '%s': %Rrc (%s)"),
                                              pszFilename, pszFileToSplit, vrc, strDstFilename.c_str());
        }
        else
            return mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                          tr("File splitter failed to construct path for '%s' in '%s': %Rrc"),
                                          pszFilename, pszFileToSplit, vrc);
    }

    return S_OK;
}

HRESULT UnattendedOs2Installer::splitFile(const char *pszFileToSplit, RTCList<RTCString> &rVecSplitFiles) RT_NOEXCEPT
{
    /*
     * Read the whole source file into memory, making sure it's zero terminated.
     */
    HRESULT hrc;
    void   *pvSrc;
    size_t  cbSrc;
    int vrc = RTFileReadAllEx(pszFileToSplit, 0 /*off*/, _16M /*cbMax*/,
                              RTFILE_RDALL_F_TRAILING_ZERO_BYTE | RTFILE_RDALL_F_FAIL_ON_MAX_SIZE | RTFILE_RDALL_O_DENY_WRITE,
                              &pvSrc, &cbSrc);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Do the actual splitting in a worker function to avoid needing to
         * thing about calling RTFileReadAllFree in error paths.
         */
        hrc = splitFileInner(pszFileToSplit, rVecSplitFiles, (const char *)pvSrc, cbSrc);
        RTFileReadAllFree(pvSrc, cbSrc);
    }
    else
        hrc = mpParent->setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Failed to read '%s' for splitting up: %Rrc"),
                                     pszFileToSplit, vrc);
    return hrc;
}

HRESULT UnattendedOs2Installer::splitFile(BaseTextScript *pEditor, RTCList<RTCString> &rVecSplitFiles) RT_NOEXCEPT
{
    /*
     * Get the output from the editor.
     */
    Utf8Str strSrc;
    HRESULT hrc = pEditor->saveToString(strSrc);
    if (SUCCEEDED(hrc))
    {
        /*
         * Do the actual splitting.
         */
        hrc = splitFileInner(pEditor->getDefaultFilename(), rVecSplitFiles, strSrc.c_str(), strSrc.length());
    }
    return hrc;
}

