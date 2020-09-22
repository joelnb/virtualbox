/* $Id$ */
/** @file
 * IPRT - RTTimeZoneGetCurrent, POSIX.
 */

/*
 * Copyright (C) 2020 Oracle Corporation
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
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/errcore.h>
#include <iprt/types.h>
#include <iprt/symlink.h>
#include <iprt/stream.h>

#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD)
 #define TZDIR                   "/usr/share/zoneinfo"
 #define TZ_MAGIC                "TZif"
#else
 #include <tzfile.h>
#endif

#define PATH_LOCALTIME           "/etc/localtime"
#if defined(RT_OS_FREEBSD)
 #define PATH_TIMEZONE           "/var/db/zoneinfo"
#else
 #define PATH_TIMEZONE           "/etc/timezone"
#endif
#define PATH_SYSCONFIG_CLOCK     "/etc/sysconfig/clock"


/**
 * Checks if a time zone database file is valid by verifying it begins with
 * TZ_MAGIC.
 *
 * @returns IPRT status code.
 * @param   pszTimezone         The time zone database file relative to
 *                              <tzfile.h>:TZDIR (normally /usr/share/zoneinfo),
 *                              e.g. Europe/London, or Etc/UTC, or UTC, or etc.
 */
static int rtIsValidTimeZoneFile(const char *pszTimeZone)
{
    int rc;

    if (pszTimeZone == NULL || *pszTimeZone == '\0' || *pszTimeZone == '/')
        return VERR_INVALID_PARAMETER;

    /* POSIX allows $TZ to begin with a colon (:) so we allow for that here */
    if (*pszTimeZone == ':')
        pszTimeZone++;

    /* construct full pathname of the time zone file */
    char szTZPath[RTPATH_MAX];
    ssize_t cchStr = RTStrPrintf2(szTZPath, sizeof(szTZPath), "%s/%s", TZDIR, pszTimeZone);
    if (cchStr <= 0)
        return VERR_BUFFER_OVERFLOW;

    /* open the time zone file and check that it begins with the correct magic number */
    RTFILE hFile = NIL_RTFILE;
    rc = RTFileOpen(&hFile, szTZPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        char achTZBuf[sizeof(TZ_MAGIC)];

        rc = RTFileRead(hFile, achTZBuf, sizeof(achTZBuf), NULL);
        if (RT_SUCCESS(rc))
        {
            if (RTStrNCmp(achTZBuf, RT_STR_TUPLE(TZ_MAGIC)))
            {
                (void) RTFileClose(hFile);
                return VERR_INVALID_MAGIC;
            }
        }
        (void) RTFileClose(hFile);
    }

    return rc;
}


/**
 * Return the system time zone.
 *
 * @returns IPRT status code.
 * @param   pszName         The buffer to return the time zone in.
 * @param   cbName          The size of the pszName buffer.
 */
RTDECL(int) RTTimeZoneGetCurrent(char *pszName, size_t cbName)
{
    int rc = RTEnvGetEx(RTENV_DEFAULT, "TZ", pszName, cbName, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * $TZ can have two different formats and one of them doesn't specify
         * a time zone database file under <tzfile.h>:TZDIR but since all
         * current callers of this routine expect a time zone filename we do
         * the validation check here so that if it is invalid then we fall back
         * to the other mechanisms to return the system's current time zone.
         */
        rc = rtIsValidTimeZoneFile(pszName);
        if (RT_SUCCESS(rc))
            return rc;
    }
    else if (rc != VERR_ENV_VAR_NOT_FOUND)
        return rc;

    /*
     * /etc/localtime is a symbolic link to the system time zone on many OSes
     * including Solaris, macOS, Ubuntu, RH/OEL 6 and later, Arch Linux, NetBSD,
     * and etc.  We extract the time zone pathname relative to TZDIR defined in
     * <tzfile.h> which is normally /usr/share/zoneinfo.
     *
     * N.B. Some OSes have /etc/localtime as a regular file instead of a
     * symlink and while we could trawl through all the files under TZDIR
     * looking for a match we instead fallback to other popular mechanisms of
     * specifying the system-wide time zone for the sake of simplicity.
     */
    const char *pszPath = PATH_LOCALTIME;
    if (RTSymlinkExists(pszPath))
    {
        char szLinkPath[RTPATH_MAX];

        rc = RTSymlinkRead(pszPath, szLinkPath, sizeof(szLinkPath), 0);
        if (RT_SUCCESS(rc))
        {
            char szLinkPathReal[RTPATH_MAX];

            /* the contents of the symink may contain '..' or other links */
            rc = RTPathReal(szLinkPath, szLinkPathReal, sizeof(szLinkPathReal));
            if (RT_SUCCESS(rc))
            {
                char szTZDirPathReal[RTPATH_MAX];

                rc = RTPathReal(TZDIR, szTZDirPathReal, sizeof(szTZDirPathReal));
                if (RT_SUCCESS(rc)) {
                    /* <tzfile.h>:TZDIR doesn't include a trailing slash */
                    rc = rtIsValidTimeZoneFile(szLinkPathReal + strlen(szTZDirPathReal) + 1);
                    if (RT_SUCCESS(rc))
                        return RTStrCopy(pszName, cbName, szLinkPathReal + strlen(szTZDirPathReal) + 1);
                }
            }
        }
        return rc;
    }

    /*
     * /etc/timezone is a regular file consisting of a single line containing
     * the time zone (e.g. Europe/London or Etc/UTC or etc.) and is used by a
     * variety of Linux distros such as Ubuntu, Gentoo, Debian, and etc.
     * The equivalent on FreeBSD is /var/db/zoneinfo.
     */
    pszPath = PATH_TIMEZONE;
    if (RTFileExists(pszPath))
    {
        RTFILE hFile = NIL_RTFILE;
        rc = RTFileOpen(&hFile, PATH_TIMEZONE, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            char achBuf[_1K];
            size_t cbRead = 0;

            rc = RTFileRead(hFile, achBuf, sizeof(achBuf), &cbRead);
            if (RT_SUCCESS(rc) && cbRead > 0)
            {
                size_t offNewLine = RTStrOffCharOrTerm(achBuf, '\n');
                /* in case no newline or terminating NULL is found */
                offNewLine = RT_MIN(offNewLine, sizeof(achBuf) - 1);
                achBuf[offNewLine] = '\0';

                rc = rtIsValidTimeZoneFile(achBuf);
                if (RT_SUCCESS(rc))
                {
                    (void) RTFileClose(hFile);
                    return RTStrCopy(pszName, cbName, achBuf);
                }
            }
            (void) RTFileClose(hFile);
        }
        return rc;
    }

    /*
     * Older versions of RedHat / OEL don't have /etc/localtime as a symlink or
     * /etc/timezone but instead have /etc/sysconfig/clock which contains a line
     * of the syntax ZONE=Europe/London amongst other entries.
     */
    pszPath = PATH_SYSCONFIG_CLOCK;
    if (RTFileExists(pszPath))
    {
        PRTSTREAM pStrm;

        rc = RTStrmOpen(pszPath, "r", &pStrm);
        if (RT_SUCCESS(rc))
        {
            char szLine[_1K];

            while (RT_SUCCESS(rc = RTStrmGetLine(pStrm, szLine, sizeof(szLine))))
            {
                if (RTStrNCmp(szLine, RT_STR_TUPLE("ZONE=")))
                    continue;

                rc = rtIsValidTimeZoneFile(szLine + strlen("ZONE="));
                if (RT_SUCCESS(rc))
                {
                    RTStrmClose(pStrm);
                    return RTStrCopy(pszName, cbName, szLine + strlen("ZONE="));
                }
            }
            RTStrmClose(pStrm);
        }
        return rc;
    }

    return rc;
}

