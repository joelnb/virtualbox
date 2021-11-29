/* $Id$ */
/** @file
 * IPRT - Process Creation, POSIX.
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
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <iprt/cdefs.h>
#ifdef RT_OS_LINUX
# define IPRT_WITH_DYNAMIC_CRYPT_R
#endif
#if (defined(RT_OS_LINUX) || defined(RT_OS_OS2)) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE
#endif

#ifdef RT_OS_OS2
# define crypt   unistd_crypt
# define setkey  unistd_setkey
# define encrypt unistd_encrypt
# include <unistd.h>
# undef  crypt
# undef  setkey
# undef  encrypt
#else
# include <unistd.h>
#endif
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#if defined(RT_OS_LINUX) || defined(RT_OS_OS2) || defined(RT_OS_SOLARIS)
# include <crypt.h>
#endif
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
# include <shadow.h>
#endif

#if defined(RT_OS_LINUX) || defined(RT_OS_OS2)
/* While Solaris has posix_spawn() of course we don't want to use it as
 * we need to have the child in a different process contract, no matter
 * whether it is started detached or not. */
# define HAVE_POSIX_SPAWN 1
#endif
#if defined(RT_OS_DARWIN) && defined(MAC_OS_X_VERSION_MIN_REQUIRED)
# if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
#  define HAVE_POSIX_SPAWN 1
# endif
#endif
#ifdef HAVE_POSIX_SPAWN
# include <spawn.h>
#endif

#if !defined(IPRT_USE_PAM) && ( defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) || defined(RT_OS_OPENBSD) )
# define IPRT_USE_PAM
#endif
#ifdef IPRT_USE_PAM
# ifdef RT_OS_DARWIN
#  include <mach-o/dyld.h>
#  define IPRT_LIBPAM_FILE      "libpam.dylib"
#  define IPRT_PAM_SERVICE_NAME "login"     /** @todo we've been abusing 'login' here, probably not needed? */
# else
#  define IPRT_LIBPAM_FILE      "libpam.so"
#  define IPRT_PAM_SERVICE_NAME "iprt-as-user"
# endif
# include <security/pam_appl.h>
# include <stdlib.h>
# include <dlfcn.h>
# include <iprt/asm.h>
#endif

#ifdef RT_OS_SOLARIS
# include <limits.h>
# include <sys/ctfs.h>
# include <sys/contract/process.h>
# include <libcontract.h>
#endif

#ifndef RT_OS_SOLARIS
# include <paths.h>
#else
# define _PATH_MAILDIR "/var/mail"
# define _PATH_DEFPATH "/usr/bin:/bin"
# define _PATH_STDPATH "/sbin:/usr/sbin:/bin:/usr/bin"
#endif
#ifndef _PATH_BSHELL
# define _PATH_BSHELL "/bin/sh"
#endif


#include <iprt/process.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#ifdef IPRT_WITH_DYNAMIC_CRYPT_R
# include <iprt/ldr.h>
#endif
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/socket.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include "internal/process.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef IPRT_USE_PAM
/** For passing info between rtCheckCredentials and rtPamConv. */
typedef struct RTPROCPAMARGS
{
    const char *pszUser;
    const char *pszPassword;
} RTPROCPAMARGS;
/** Pointer to rtPamConv argument package. */
typedef RTPROCPAMARGS *PRTPROCPAMARGS;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Environment dump marker used with CSH.   */
static const char g_szEnvMarkerBegin[] = "IPRT_EnvEnvEnv_Begin_EnvEnvEnv";
/** Environment dump marker used with CSH.   */
static const char g_szEnvMarkerEnd[]   = "IPRT_EnvEnvEnv_End_EnvEnvEnv";


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtProcPosixCreateInner(const char *pszExec, const char * const *papszArgs, RTENV hEnv, RTENV hEnvToUse,
                                  uint32_t fFlags, const char *pszAsUser, uid_t uid, gid_t gid,
                                  unsigned cRedirFds, int *paRedirFds, PRTPROCESS phProcess);


#ifdef IPRT_USE_PAM
/**
 * Worker for rtCheckCredentials that feeds password and maybe username to PAM.
 *
 * @returns PAM status.
 * @param   cMessages       Number of messages.
 * @param   papMessages     Message vector.
 * @param   ppaResponses    Where to put our responses.
 * @param   pvAppData       Pointer to RTPROCPAMARGS.
 */
static int rtPamConv(int cMessages, const struct pam_message **papMessages, struct pam_response **ppaResponses, void *pvAppData)
{
    LogFlow(("rtPamConv: cMessages=%d\n", cMessages));
    PRTPROCPAMARGS pArgs = (PRTPROCPAMARGS)pvAppData;
    AssertPtrReturn(pArgs, PAM_CONV_ERR);

    struct pam_response *paResponses = (struct pam_response *)calloc(cMessages, sizeof(paResponses[0]));
    AssertReturn(paResponses,  PAM_CONV_ERR);
    for (int i = 0; i < cMessages; i++)
    {
        LogFlow(("rtPamConv: #%d: msg_style=%d msg=%s\n", i, papMessages[i]->msg_style, papMessages[i]->msg));

        paResponses[i].resp_retcode = 0;
        if (papMessages[i]->msg_style == PAM_PROMPT_ECHO_OFF)
            paResponses[i].resp = strdup(pArgs->pszPassword);
        else if (papMessages[i]->msg_style == PAM_PROMPT_ECHO_ON)
            paResponses[i].resp = strdup(pArgs->pszUser);
        else
        {
            paResponses[i].resp = NULL;
            continue;
        }
        if (paResponses[i].resp == NULL)
        {
            while (i-- > 0)
                free(paResponses[i].resp);
            free(paResponses);
            LogFlow(("rtPamConv: out of memory\n"));
            return PAM_CONV_ERR;
        }
    }

    *ppaResponses = paResponses;
    return PAM_SUCCESS;
}
#endif /* IPRT_USE_PAM */


#if defined(IPRT_WITH_DYNAMIC_CRYPT_R) && !defined(IPRT_USE_PAM)
/** Pointer to crypt_r(). */
typedef char *(*PFNCRYPTR)(const char *, const char *, struct crypt_data *);

/**
 * Wrapper for resolving and calling crypt_r dynamcially.
 *
 * The reason for this is that fedora 30+ wants to use libxcrypt rather than the
 * glibc libcrypt.  The two libraries has different crypt_data sizes and layout,
 * so we allocate a 256KB data block to be on the safe size (caller does this).
 */
static char *rtProcDynamicCryptR(const char *pszKey, const char *pszSalt, struct crypt_data *pData)
{
    static PFNCRYPTR volatile s_pfnCryptR = NULL;
    PFNCRYPTR pfnCryptR = s_pfnCryptR;
    if (pfnCryptR)
        return pfnCryptR(pszKey, pszSalt, pData);

    pfnCryptR = (PFNCRYPTR)(uintptr_t)RTLdrGetSystemSymbolEx("libcrypt.so", "crypt_r", RTLDRLOAD_FLAGS_SO_VER_RANGE(1, 6));
    if (!pfnCryptR)
        pfnCryptR = (PFNCRYPTR)(uintptr_t)RTLdrGetSystemSymbolEx("libxcrypt.so", "crypt_r", RTLDRLOAD_FLAGS_SO_VER_RANGE(1, 32));
    if (pfnCryptR)
    {
        s_pfnCryptR = pfnCryptR;
        return pfnCryptR(pszKey, pszSalt, pData);
    }

    LogRel(("IPRT/RTProc: Unable to locate crypt_r!\n"));
    return NULL;
}
#endif /* IPRT_WITH_DYNAMIC_CRYPT_R */


/**
 * Check the credentials and return the gid/uid of user.
 *
 * @param    pszUser     username
 * @param    pszPasswd   password
 * @param    gid         where to store the GID of the user
 * @param    uid         where to store the UID of the user
 * @returns IPRT status code
 */
static int rtCheckCredentials(const char *pszUser, const char *pszPasswd, gid_t *pGid, uid_t *pUid)
{
#ifdef IPRT_USE_PAM
    RTLogPrintf("rtCheckCredentials\n");

    /*
     * Resolve user to UID and GID.
     */
    char            szBuf[_4K];
    struct passwd   Pw;
    struct passwd  *pPw;
    if (getpwnam_r(pszUser, &Pw, szBuf, sizeof(szBuf), &pPw) != 0)
        return VERR_AUTHENTICATION_FAILURE;
    if (!pPw)
        return VERR_AUTHENTICATION_FAILURE;

    *pUid = pPw->pw_uid;
    *pGid = pPw->pw_gid;

    /*
     * Use PAM for the authentication.
     * Note! libpam.2.dylib was introduced with 10.6.x (OpenPAM).
     */
    void *hModPam = dlopen(IPRT_LIBPAM_FILE, RTLD_LAZY | RTLD_GLOBAL);
    if (hModPam)
    {
        int (*pfnPamStart)(const char *, const char *, struct pam_conv *, pam_handle_t **);
        int (*pfnPamAuthenticate)(pam_handle_t *, int);
        int (*pfnPamAcctMgmt)(pam_handle_t *, int);
        int (*pfnPamSetItem)(pam_handle_t *, int, const void *);
        int (*pfnPamEnd)(pam_handle_t *, int);
        *(void **)&pfnPamStart        = dlsym(hModPam, "pam_start");
        *(void **)&pfnPamAuthenticate = dlsym(hModPam, "pam_authenticate");
        *(void **)&pfnPamAcctMgmt     = dlsym(hModPam, "pam_acct_mgmt");
        *(void **)&pfnPamSetItem      = dlsym(hModPam, "pam_set_item");
        *(void **)&pfnPamEnd          = dlsym(hModPam, "pam_end");
        ASMCompilerBarrier();
        if (   pfnPamStart
            && pfnPamAuthenticate
            && pfnPamAcctMgmt
            && pfnPamSetItem
            && pfnPamEnd)
        {
# define pam_start           pfnPamStart
# define pam_authenticate    pfnPamAuthenticate
# define pam_acct_mgmt       pfnPamAcctMgmt
# define pam_set_item        pfnPamSetItem
# define pam_end             pfnPamEnd

            /* Do the PAM stuff. */
            pam_handle_t   *hPam        = NULL;
            RTPROCPAMARGS   PamConvArgs = { pszUser, pszPasswd };
            struct pam_conv PamConversation;
            RT_ZERO(PamConversation);
            PamConversation.appdata_ptr = &PamConvArgs;
            PamConversation.conv        = rtPamConv;
            int rc = pam_start(IPRT_PAM_SERVICE_NAME, pszUser, &PamConversation, &hPam);
            if (rc == PAM_SUCCESS)
            {
                rc = pam_set_item(hPam, PAM_RUSER, pszUser);
                if (rc == PAM_SUCCESS)
                    rc = pam_authenticate(hPam, 0);
                if (rc == PAM_SUCCESS)
                {
                    rc = pam_acct_mgmt(hPam, 0);
                    if (   rc == PAM_SUCCESS
                        || rc == PAM_AUTHINFO_UNAVAIL /*??*/)
                    {
                        pam_end(hPam, PAM_SUCCESS);
                        dlclose(hModPam);
                        return VINF_SUCCESS;
                    }
                    Log(("rtCheckCredentials: pam_acct_mgmt -> %d\n", rc));
                }
                else
                    Log(("rtCheckCredentials: pam_authenticate -> %d\n", rc));
                pam_end(hPam, rc);
            }
            else
                Log(("rtCheckCredentials: pam_start -> %d\n", rc));
        }
        else
            Log(("rtCheckCredentials: failed to resolve symbols: %p %p %p %p %p\n",
                 pfnPamStart, pfnPamAuthenticate, pfnPamAcctMgmt, pfnPamSetItem, pfnPamEnd));
        dlclose(hModPam);
    }
    else
        Log(("rtCheckCredentials: Loading " IPRT_LIBPAM_FILE " failed\n"));
    return VERR_AUTHENTICATION_FAILURE;

#else
    /*
     * Lookup the user in /etc/passwd first.
     *
     * Note! On FreeBSD and OS/2 the root user will open /etc/shadow here, so
     *       the getspnam_r step is not necessary.
     */
    struct passwd  Pwd;
    char           szBuf[_4K];
    struct passwd *pPwd = NULL;
    if (getpwnam_r(pszUser, &Pwd, szBuf, sizeof(szBuf), &pPwd) != 0)
        return VERR_AUTHENTICATION_FAILURE;
    if (pPwd == NULL)
        return VERR_AUTHENTICATION_FAILURE;

# if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    /*
     * Ditto for /etc/shadow and replace pw_passwd from above if we can access it:
     */
    struct spwd  ShwPwd;
    char         szBuf2[_4K];
#  if defined(RT_OS_LINUX)
    struct spwd *pShwPwd = NULL;
    if (getspnam_r(pszUser, &ShwPwd, szBuf2, sizeof(szBuf2), &pShwPwd) != 0)
        pShwPwd = NULL;
#  else
    struct spwd *pShwPwd = getspnam_r(pszUser, &ShwPwd, szBuf2, sizeof(szBuf2));
#  endif
    if (pShwPwd != NULL)
        pPwd->pw_passwd = pShwPwd->sp_pwdp;
# endif

    /*
     * Encrypt the passed in password and see if it matches.
     */
# if !defined(RT_OS_LINUX)
    int rc;
# else
    /* Default fCorrect=true if no password specified. In that case, pPwd->pw_passwd
       must be NULL (no password set for this user). Fail if a password is specified
       but the user does not have one assigned. */
    int rc = !pszPasswd || !*pszPasswd ? VINF_SUCCESS : VERR_AUTHENTICATION_FAILURE;
    if (pPwd->pw_passwd && *pPwd->pw_passwd)
# endif
    {
# if defined(RT_OS_LINUX) || defined(RT_OS_OS2)
#  ifdef IPRT_WITH_DYNAMIC_CRYPT_R
        size_t const       cbCryptData = RT_MAX(sizeof(struct crypt_data) * 2, _256K);
#  else
        size_t const       cbCryptData = sizeof(struct crypt_data);
#  endif
        struct crypt_data *pCryptData  = (struct crypt_data *)RTMemTmpAllocZ(cbCryptData);
        if (pCryptData)
        {
#  ifdef IPRT_WITH_DYNAMIC_CRYPT_R
            char *pszEncPasswd = rtProcDynamicCryptR(pszPasswd, pPwd->pw_passwd, pCryptData);
#  else
            char *pszEncPasswd = crypt_r(pszPasswd, pPwd->pw_passwd, pCryptData);
#  endif
            rc = pszEncPasswd && !strcmp(pszEncPasswd, pPwd->pw_passwd) ? VINF_SUCCESS : VERR_AUTHENTICATION_FAILURE;
            RTMemWipeThoroughly(pCryptData, cbCryptData, 3);
            RTMemTmpFree(pCryptData);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
# else
        char *pszEncPasswd = crypt(pszPasswd, pPwd->pw_passwd);
        rc = strcmp(pszEncPasswd, pPwd->pw_passwd) == 0 ? VINF_SUCCESS : VERR_AUTHENTICATION_FAILURE;
# endif
    }

    /*
     * Return GID and UID on success.  Always wipe stack buffers.
     */
    if (RT_SUCCESS(rc))
    {
        *pGid = pPwd->pw_gid;
        *pUid = pPwd->pw_uid;
    }
    RTMemWipeThoroughly(szBuf, sizeof(szBuf), 3);
# if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    RTMemWipeThoroughly(szBuf2, sizeof(szBuf2), 3);
# endif
    return rc;
#endif
}

#ifdef RT_OS_SOLARIS

/** @todo the error reporting of the Solaris process contract code could be
 * a lot better, but essentially it is not meant to run into errors after
 * the debugging phase. */
static int rtSolarisContractPreFork(void)
{
    int templateFd = open64(CTFS_ROOT "/process/template", O_RDWR);
    if (templateFd < 0)
        return -1;

    /* Set template parameters and event sets. */
    if (ct_pr_tmpl_set_param(templateFd, CT_PR_PGRPONLY))
    {
        close(templateFd);
        return -1;
    }
    if (ct_pr_tmpl_set_fatal(templateFd, CT_PR_EV_HWERR))
    {
        close(templateFd);
        return -1;
    }
    if (ct_tmpl_set_critical(templateFd, 0))
    {
        close(templateFd);
        return -1;
    }
    if (ct_tmpl_set_informative(templateFd, CT_PR_EV_HWERR))
    {
        close(templateFd);
        return -1;
    }

    /* Make this the active template for the process. */
    if (ct_tmpl_activate(templateFd))
    {
        close(templateFd);
        return -1;
    }

    return templateFd;
}

static void rtSolarisContractPostForkChild(int templateFd)
{
    if (templateFd == -1)
        return;

    /* Clear the active template. */
    ct_tmpl_clear(templateFd);
    close(templateFd);
}

static void rtSolarisContractPostForkParent(int templateFd, pid_t pid)
{
    if (templateFd == -1)
        return;

    /* Clear the active template. */
    int cleared = ct_tmpl_clear(templateFd);
    close(templateFd);

    /* If the clearing failed or the fork failed there's nothing more to do. */
    if (cleared || pid <= 0)
        return;

    /* Look up the contract which was created by this thread. */
    int statFd = open64(CTFS_ROOT "/process/latest", O_RDONLY);
    if (statFd == -1)
        return;
    ct_stathdl_t statHdl;
    if (ct_status_read(statFd, CTD_COMMON, &statHdl))
    {
        close(statFd);
        return;
    }
    ctid_t ctId = ct_status_get_id(statHdl);
    ct_status_free(statHdl);
    close(statFd);
    if (ctId < 0)
        return;

    /* Abandon this contract we just created. */
    char ctlPath[PATH_MAX];
    size_t len = snprintf(ctlPath, sizeof(ctlPath),
                          CTFS_ROOT "/process/%ld/ctl", (long)ctId);
    if (len >= sizeof(ctlPath))
        return;
    int ctlFd = open64(ctlPath, O_WRONLY);
    if (statFd == -1)
        return;
    if (ct_ctl_abandon(ctlFd) < 0)
    {
        close(ctlFd);
        return;
    }
    close(ctlFd);
}

#endif /* RT_OS_SOLARIS */


RTR3DECL(int)   RTProcCreate(const char *pszExec, const char * const *papszArgs, RTENV Env, unsigned fFlags, PRTPROCESS pProcess)
{
    return RTProcCreateEx(pszExec, papszArgs, Env, fFlags,
                          NULL, NULL, NULL,  /* standard handles */
                          NULL /*pszAsUser*/, NULL /* pszPassword*/, NULL /*pvExtraData*/,
                          pProcess);
}


/**
 * Adjust the profile environment after forking the child process and changing
 * the UID.
 *
 * @returns IRPT status code.
 * @param   hEnvToUse       The environment we're going to use with execve.
 * @param   fFlags          The process creation flags.
 * @param   hEnv            The environment passed in by the user.
 */
static int rtProcPosixAdjustProfileEnvFromChild(RTENV hEnvToUse, uint32_t fFlags, RTENV hEnv)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_DARWIN
    if (   RT_SUCCESS(rc)
        && (!(fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD) || RTEnvExistEx(hEnv, "TMPDIR")) )
    {
        char szValue[_4K];
        size_t cbNeeded = confstr(_CS_DARWIN_USER_TEMP_DIR, szValue, sizeof(szValue));
        if (cbNeeded > 0 && cbNeeded < sizeof(szValue))
        {
            char *pszTmp;
            rc = RTStrCurrentCPToUtf8(&pszTmp, szValue);
            if (RT_SUCCESS(rc))
            {
                rc = RTEnvSetEx(hEnvToUse, "TMPDIR", pszTmp);
                RTStrFree(pszTmp);
            }
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
#else
    RT_NOREF_PV(hEnvToUse); RT_NOREF_PV(fFlags); RT_NOREF_PV(hEnv);
#endif
    return rc;
}


/**
 * Undos quoting and escape sequences and looks for stop characters.
 *
 * @returns Where to continue scanning in @a pszString.  This points to the
 *          next character after the stop character, but for the zero terminator
 *          it points to the terminator character.
 * @param   pszString           The string to undo quoting and escaping for.
 *                              This is both input and output as the work is
 *                              done in place.
 * @param   pfStoppedOnEqual    Where to return whether we stopped work on a
 *                              plain equal characater or not.  If this is NULL,
 *                              then the equal character is not a stop
 *                              character, then only newline and the zero
 *                              terminator are.
 */
static char *rtProcPosixProfileEnvUnquoteAndUnescapeString(char *pszString, bool *pfStoppedOnEqual)
{
    if (pfStoppedOnEqual)
        *pfStoppedOnEqual = false;

    enum { kPlain, kSingleQ, kDoubleQ } enmState = kPlain;
    char *pszDst = pszString;
    for (;;)
    {
        char ch = *pszString++;
        switch (ch)
        {
            default:
                *pszDst++ = ch;
                break;

            case '\\':
            {
                char ch2;
                if (   enmState == kSingleQ
                    || (ch2 = *pszString) == '\0'
                    || (enmState == kDoubleQ && strchr("\\$`\"\n", ch2) == NULL) )
                    *pszDst++ = ch;
                else
                {
                    *pszDst++ = ch2;
                    pszString++;
                }
                break;
            }

            case '"':
                if (enmState == kSingleQ)
                    *pszDst++ = ch;
                else
                    enmState = enmState == kPlain ? kDoubleQ : kPlain;
                break;

            case '\'':
                if (enmState == kDoubleQ)
                    *pszDst++ = ch;
                else
                    enmState = enmState == kPlain ? kSingleQ : kPlain;
                break;

            case '\n':
                if (enmState == kPlain)
                {
                    *pszDst = '\0';
                    return pszString;
                }
                *pszDst++ = ch;
                break;

            case '=':
                if (enmState == kPlain && pfStoppedOnEqual)
                {
                    *pszDst = '\0';
                    *pfStoppedOnEqual = true;
                    return pszString;
                }
                *pszDst++ = ch;
                break;

            case '\0':
                Assert(enmState == kPlain);
                *pszDst = '\0';
                return pszString - 1;
        }
    }
}


/**
 * Worker for rtProcPosixProfileEnvRunAndHarvest that parses the environment
 * dump and loads it into hEnvToUse.
 *
 * @note    This isn't entirely correct should any of the profile setup scripts
 *          unset any of the environment variables in the basic initial
 *          enviornment, but since that's unlikely and it's very convenient to
 *          have something half sensible as a basis if don't don't grok the dump
 *          entirely and would skip central stuff like PATH or HOME.
 *
 * @returns IPRT status code.
 * @retval  -VERR_PARSE_ERROR (positive, e.g. warning) if we run into trouble.
 * @retval  -VERR_INVALID_UTF8_ENCODING (positive, e.g. warning) if there are
 *          invalid UTF-8 in the environment.  This isn't unlikely if the
 *          profile doesn't use UTF-8.  This is unfortunately not something we
 *          can guess to accurately up front, so we don't do any guessing and
 *          hope everyone is sensible and use UTF-8.
 * @param   hEnvToUse       The basic environment to extend with what we manage
 *                          to parse here.
 * @param   pszEnvDump      The environment dump to parse.  Nominally in Bourne
 *                          shell 'export -p' format.
 * @param   fWithMarkers    Whether there are markers around the dump (C shell,
 *                          tmux) or not.
 */
static int rtProcPosixProfileEnvHarvest(RTENV hEnvToUse, char *pszEnvDump, bool fWithMarkers)
{
    LogFunc(("**** pszEnvDump start ****\n%s**** pszEnvDump end ****\n", pszEnvDump));

    /*
     * Clip dump at markers if we're using them (C shell).
     */
    if (fWithMarkers)
    {
        char *pszStart = strstr(pszEnvDump, g_szEnvMarkerBegin);
        AssertReturn(pszStart, -VERR_PARSE_ERROR);
        pszStart += sizeof(g_szEnvMarkerBegin) - 1;
        if (*pszStart == '\n')
            pszStart++;
        pszEnvDump = pszStart;

        char *pszEnd = strstr(pszStart, g_szEnvMarkerEnd);
        AssertReturn(pszEnd, -VERR_PARSE_ERROR);
        *pszEnd = '\0';
    }

    /*
     * Since we're using /bin/sh -c "export -p" for all the dumping, we should
     * always get lines on the format:
     *     export VAR1="Value 1"
     *     export VAR2=Value2
     *
     * However, just in case something goes wrong, like bash doesn't think it
     * needs to be posixly correct, try deal with the alternative where
     * "declare -x " replaces the "export".
     */
    const char *pszPrefix;
    if (   strncmp(pszEnvDump, RT_STR_TUPLE("export")) == 0
        && RT_C_IS_BLANK(pszEnvDump[6]))
        pszPrefix = "export ";
    else if (   strncmp(pszEnvDump, RT_STR_TUPLE("declare")) == 0
             && RT_C_IS_BLANK(pszEnvDump[7])
             && pszEnvDump[8] == '-')
        pszPrefix = "declare -x "; /* We only need to care about the non-array, non-function lines. */
    else
        AssertFailedReturn(-VERR_PARSE_ERROR);
    size_t const cchPrefix = strlen(pszPrefix);

    /*
     * Process the lines, ignoring stuff which we don't grok.
     * The shell should quote problematic characters. Bash double quotes stuff
     * by default, whereas almquist's shell does it as needed and only the value
     * side.
     */
    int rc = VINF_SUCCESS;
    while (pszEnvDump && *pszEnvDump != '\0')
    {
        /*
         * Skip the prefixing command.
         */
        if (   cchPrefix == 0
            || strncmp(pszEnvDump, pszPrefix, cchPrefix) == 0)
        {
            pszEnvDump += cchPrefix;
            while (RT_C_IS_BLANK(*pszEnvDump))
                pszEnvDump++;
        }
        else
        {
            /* Oops, must find our bearings for some reason... */
            pszEnvDump = strchr(pszEnvDump, '\n');
            rc = -VERR_PARSE_ERROR;
            continue;
        }

        /*
         * Parse out the variable name using typical bourne shell escaping
         * and quoting rules.
         */
        /** @todo We should throw away lines that aren't propertly quoted, now we
         *        just continue and use what we found. */
        const char *pszVar               = pszEnvDump;
        bool        fStoppedOnPlainEqual = false;
        pszEnvDump = rtProcPosixProfileEnvUnquoteAndUnescapeString(pszEnvDump, &fStoppedOnPlainEqual);
        const char *pszValue             = pszEnvDump;
        if (fStoppedOnPlainEqual)
            pszEnvDump = rtProcPosixProfileEnvUnquoteAndUnescapeString(pszEnvDump, NULL /*pfStoppedOnPlainEqual*/);
        else
            pszValue = "";

        /*
         * Add them if valid UTF-8, otherwise we simply drop them for now.
         * The whole codeset stuff goes seriously wonky here as the environment
         * we're harvesting probably contains it's own LC_CTYPE or LANG variables,
         * so ignore the problem for now.
         */
        if (   RTStrIsValidEncoding(pszVar)
            && RTStrIsValidEncoding(pszValue))
        {
            int rc2 = RTEnvSetEx(hEnvToUse, pszVar, pszValue);
            AssertRCReturn(rc2, rc2);
        }
        else if (rc == VINF_SUCCESS)
            rc = -VERR_INVALID_UTF8_ENCODING;
    }

    return rc;
}


/**
 * Runs the user's shell in login mode with some environment dumping logic and
 * harvests the dump, putting it into hEnvToUse.
 *
 * This is a bit hairy, esp. with regards to codesets.
 *
 * @returns IPRT status code.  Not all error statuses will be returned and the
 *          caller should just continue with whatever is in hEnvToUse.
 * @param   hEnvToUse   On input this is the basic user environment, on success
 *                      in is fleshed out with stuff from the login shell dump.
 * @param   pszAsUser   The user name for the profile.  NULL if the current
 *                      user.
 * @param   uid         The UID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   gid         The GID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   pszShell    The login shell.  This is a writable string to avoid
 *                      needing to make a copy of it when examining the path
 *                      part, instead we make a temporary change to it which is
 *                      always reverted before returning.
 */
static int rtProcPosixProfileEnvRunAndHarvest(RTENV hEnvToUse, const char *pszAsUser, uid_t uid, gid_t gid, char *pszShell)
{
    LogFlowFunc(("pszAsUser=%s uid=%u gid=%u pszShell=%s; hEnvToUse contains %u variables on entry\n",
                 pszAsUser, uid, gid, pszShell, RTEnvCountEx(hEnvToUse) ));

    /*
     * The three standard handles should be pointed to /dev/null, the 3rd handle
     * is used to dump the environment.
     */
    RTPIPE hPipeR, hPipeW;
    int rc = RTPipeCreate(&hPipeR, &hPipeW, 0);
    if (RT_SUCCESS(rc))
    {
        RTFILE hFileNull;
        rc = RTFileOpenBitBucket(&hFileNull, RTFILE_O_READWRITE);
        if (RT_SUCCESS(rc))
        {
            int aRedirFds[4];
            aRedirFds[0] = aRedirFds[1] = aRedirFds[2] = RTFileToNative(hFileNull);
            aRedirFds[3] = RTPipeToNative(hPipeW);

            /*
             * Allocate a buffer for receiving the environment dump.
             *
             * This is fixed sized for simplicity and safety (creative user script
             * shouldn't be allowed to exhaust our memory or such, after all we're
             * most likely running with root privileges in this code path).
             */
            size_t const  cbEnvDump  = _64K;
            char  * const pszEnvDump = (char *)RTMemTmpAllocZ(cbEnvDump);
            if (pszEnvDump)
            {
                /*
                 * Our default approach is using /bin/sh:
                 */
                const char *pszExec = _PATH_BSHELL;
                const char *apszArgs[8];
                apszArgs[0] = "-sh";        /* First arg must start with a dash for login shells. */
                apszArgs[1] = "-c";
                apszArgs[2] = "POSIXLY_CORRECT=1;export -p >&3";
                apszArgs[3] = NULL;

                /*
                 * But see if we can trust the shell to be a real usable shell.
                 * This would be great as different shell typically has different profile setup
                 * files and we'll endup with the wrong enviornment if we use a different shell.
                 */
                char        szDashShell[32];
                char        szExportArg[128];
                bool        fWithMarkers = false;
                const char *pszShellNm   = RTPathFilename(pszShell);
                if (   pszShellNm
                    && access(pszShellNm, X_OK))
                {
                    /*
                     * First the check that it's a known bin directory:
                     */
                    size_t const cchShellPath = pszShellNm - pszShell;
                    char const   chSaved = pszShell[cchShellPath - 1];
                    pszShell[cchShellPath - 1] = '\0';
                    if (   RTPathCompare(pszShell, "/bin") == 0
                        || RTPathCompare(pszShell, "/usr/bin") == 0
                        || RTPathCompare(pszShell, "/usr/local/bin") == 0)
                    {
                        /*
                         * Then see if we recognize the shell name.
                         */
                        RTStrCopy(&szDashShell[1], sizeof(szDashShell) - 1, pszShellNm);
                        szDashShell[0] = '-';
                        if (   strcmp(pszShellNm, "bash") == 0
                            || strcmp(pszShellNm, "ksh") == 0
                            || strcmp(pszShellNm, "ksh93") == 0
                            || strcmp(pszShellNm, "zsh") == 0)
                        {
                            pszExec      = pszShell;
                            apszArgs[0]  = szDashShell;

                            /* Use /bin/sh for doing the environment dumping so we get the same kind
                               of output from everyone and can limit our parsing + testing efforts. */
                            RTStrPrintf(szExportArg, sizeof(szExportArg),
                                        "%s -c 'POSIXLY_CORRECT=1;export -p >&3'", _PATH_BSHELL);
                            apszArgs[2]  = szExportArg;
                        }
                        /* C shell is very annoying in that it closes fd 3 without regard to what
                           we might have put there, so we must use stdout here but with markers so
                           we can find the dump.
                           Seems tmux have similar issues as it doesn't work above, but works fine here. */
                        else if (   strcmp(pszShellNm, "csh") == 0
                                 || strcmp(pszShellNm, "tcsh") == 0
                                 || strcmp(pszShellNm, "tmux") == 0)
                        {
                            pszExec      = pszShell;
                            apszArgs[0]  = szDashShell;

                            fWithMarkers = true;
                            size_t cch = RTStrPrintf(szExportArg, sizeof(szExportArg),
                                                     "%s -c 'set -e;POSIXLY_CORRECT=1;echo %s;export -p;echo %s'",
                                                     _PATH_BSHELL, g_szEnvMarkerBegin, g_szEnvMarkerEnd);
                            Assert(cch < sizeof(szExportArg) - 1); RT_NOREF(cch);
                            apszArgs[2]  = szExportArg;

                            aRedirFds[1] = aRedirFds[3];
                            aRedirFds[3] = -1;
                        }
                    }
                    pszShell[cchShellPath - 1] = chSaved;
                }

                /*
                 * Create the process and wait for the output.
                 */
                LogFunc(("Executing '%s': '%s', '%s', '%s'\n", pszExec, apszArgs[0], apszArgs[1], apszArgs[2]));
                RTPROCESS hProcess = NIL_RTPROCESS;
                rc = rtProcPosixCreateInner(pszExec, apszArgs, hEnvToUse, hEnvToUse, 0 /*fFlags*/,
                                            pszAsUser, uid, gid, RT_ELEMENTS(aRedirFds), aRedirFds, &hProcess);
                if (RT_SUCCESS(rc))
                {
                    RTPipeClose(hPipeW);
                    hPipeW = NIL_RTPIPE;

                    size_t         offEnvDump = 0;
                    uint64_t const msStart    = RTTimeMilliTS();
                    for (;;)
                    {
                        size_t cbRead = 0;
                        if (offEnvDump < cbEnvDump - 1)
                        {
                            rc = RTPipeRead(hPipeR, &pszEnvDump[offEnvDump], cbEnvDump - 1 - offEnvDump, &cbRead);
                            if (RT_SUCCESS(rc))
                                offEnvDump += cbRead;
                            else
                            {
                                LogFlowFunc(("Breaking out of read loop: %Rrc\n", rc));
                                if (rc == VERR_BROKEN_PIPE)
                                    rc = VINF_SUCCESS;
                                break;
                            }
                            pszEnvDump[offEnvDump] = '\0';
                        }
                        else
                        {
                            LogFunc(("Too much data in environment dump, dropping it\n"));
                            rc = VERR_TOO_MUCH_DATA;
                            break;
                        }

                        /* Do the timout check. */
                        uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
                        if (cMsElapsed >= RT_MS_15SEC)
                        {
                            LogFunc(("Timed out after %RU64 ms\n", cMsElapsed));
                            rc = VERR_TIMEOUT;
                            break;
                        }

                        /* If we got no data in above wait for more to become ready. */
                        if (!cbRead)
                            RTPipeSelectOne(hPipeR, RT_MS_15SEC - cMsElapsed);
                    }

                    /*
                     * Kill the process and wait for it to avoid leaving zombies behind.
                     */
                    /** @todo do we check the exit code? */
                    int rc2 = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, NULL);
                    if (RT_SUCCESS(rc2))
                        LogFlowFunc(("First RTProcWait succeeded\n"));
                    else
                    {
                        LogFunc(("First RTProcWait failed (%Rrc), terminating and doing a blocking wait\n", rc2));
                        RTProcTerminate(hProcess);
                        RTProcWait(hProcess, RTPROCWAIT_FLAGS_BLOCK, NULL);
                    }

                    /*
                     * Parse the result.
                     */
                    if (RT_SUCCESS(rc))
                        rc = rtProcPosixProfileEnvHarvest(hEnvToUse, pszEnvDump, fWithMarkers);
                    else
                    {
                        LogFunc(("Ignoring rc=%Rrc from the pipe read loop and continues with basic environment\n", rc));
                        rc = -rc;
                    }
                }
                else
                    LogFunc(("Failed to create process '%s': %Rrc\n", pszExec, rc));
                RTMemTmpFree(pszEnvDump);
            }
            else
            {
                LogFunc(("Failed to allocate %#zx bytes for the dump\n", cbEnvDump));
                rc = VERR_NO_TMP_MEMORY;
            }
            RTFileClose(hFileNull);
        }
        else
            LogFunc(("Failed to open /dev/null: %Rrc\n", rc));
        RTPipeClose(hPipeR);
        RTPipeClose(hPipeW);
    }
    else
        LogFunc(("Failed to create pipe: %Rrc\n", rc));
    LogFlowFunc(("returns %Rrc (hEnvToUse contains %u variables now)\n", rc, RTEnvCountEx(hEnvToUse)));
    return rc;
}


/**
 * Create an environment for the given user.
 *
 * This starts by creating a very basic environment and then tries to do it
 * properly by running the user's shell in login mode with some environment
 * dumping attached.  The latter may fail and we'll ignore that for now and move
 * ahead with the very basic environment.
 *
 * @returns IPRT status code.
 * @param   phEnvToUse  Where to return the created environment.
 * @param   pszAsUser   The user name for the profile.  NULL if the current
 *                      user.
 * @param   uid         The UID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   gid         The GID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   fFlags      RTPROC_FLAGS_XXX
 */
static int rtProcPosixCreateProfileEnv(PRTENV phEnvToUse, const char *pszAsUser, uid_t uid, gid_t gid, uint32_t fFlags)
{
    struct passwd   Pwd;
    struct passwd  *pPwd = NULL;
    char            achBuf[_4K];
    int             rc;
    errno = 0;
    if (pszAsUser)
        rc = getpwnam_r(pszAsUser, &Pwd, achBuf, sizeof(achBuf), &pPwd);
    else
        rc = getpwuid_r(getuid(), &Pwd, achBuf, sizeof(achBuf), &pPwd);
    if (rc == 0 && pPwd)
    {
        char *pszDir;
        rc = RTStrCurrentCPToUtf8(&pszDir, pPwd->pw_dir);
        if (RT_SUCCESS(rc))
        {
#if 0 /* Enable and modify this to test shells other that your login shell. */
            pPwd->pw_shell = (char *)"/bin/tmux";
#endif
            char *pszShell;
            rc = RTStrCurrentCPToUtf8(&pszShell, pPwd->pw_shell);
            if (RT_SUCCESS(rc))
            {
                char *pszAsUserFree = NULL;
                if (!pszAsUser)
                {
                    rc = RTStrCurrentCPToUtf8(&pszAsUserFree, pPwd->pw_name);
                    if (RT_SUCCESS(rc))
                        pszAsUser = pszAsUserFree;
                }
                if (RT_SUCCESS(rc))
                {
                    rc = RTEnvCreate(phEnvToUse);
                    if (RT_SUCCESS(rc))
                    {
                        RTENV hEnvToUse = *phEnvToUse;

                        rc = RTEnvSetEx(hEnvToUse, "HOME", pszDir);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "SHELL", pszShell);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "USER", pszAsUser);
                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "LOGNAME", pszAsUser);

                        if (RT_SUCCESS(rc))
                            rc = RTEnvSetEx(hEnvToUse, "PATH", pPwd->pw_uid == 0 ? _PATH_STDPATH : _PATH_DEFPATH);

                        if (RT_SUCCESS(rc))
                        {
                            RTStrPrintf(achBuf, sizeof(achBuf), "%s/%s", _PATH_MAILDIR, pszAsUser);
                            rc = RTEnvSetEx(hEnvToUse, "MAIL", achBuf);
                        }

#ifdef RT_OS_DARWIN
                        if (RT_SUCCESS(rc) && !pszAsUserFree)
                        {
                            size_t cbNeeded = confstr(_CS_DARWIN_USER_TEMP_DIR, achBuf, sizeof(achBuf));
                            if (cbNeeded > 0 && cbNeeded < sizeof(achBuf))
                            {
                                char *pszTmp;
                                rc = RTStrCurrentCPToUtf8(&pszTmp, achBuf);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTEnvSetEx(hEnvToUse, "TMPDIR", pszTmp);
                                    RTStrFree(pszTmp);
                                }
                            }
                            else
                                rc = VERR_BUFFER_OVERFLOW;
                        }
#endif
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Now comes the fun part where we need to try run a shell in login mode
                             * and harvest its final environment to get the proper environment for
                             * the user.  We ignore some failures here so buggy login scrips and
                             * other weird stuff won't trip us up too badly.
                             */
                            if (!(fFlags & RTPROC_FLAGS_ONLY_BASIC_PROFILE))
                                rc = rtProcPosixProfileEnvRunAndHarvest(hEnvToUse, pszAsUser, uid, gid, pszShell);
                        }

                        if (RT_FAILURE(rc))
                            RTEnvDestroy(hEnvToUse);
                    }
                    RTStrFree(pszAsUserFree);
                }
                RTStrFree(pszShell);
            }
            RTStrFree(pszDir);
        }
    }
    else
        rc = errno ? RTErrConvertFromErrno(errno) : VERR_ACCESS_DENIED;
    return rc;
}


/**
 * RTPathTraverseList callback used by RTProcCreateEx to locate the executable.
 */
static DECLCALLBACK(int) rtPathFindExec(char const *pchPath, size_t cchPath, void *pvUser1, void *pvUser2)
{
    const char *pszExec     = (const char *)pvUser1;
    char       *pszRealExec = (char *)pvUser2;
    int rc = RTPathJoinEx(pszRealExec, RTPATH_MAX, pchPath, cchPath, pszExec, RTSTR_MAX);
    if (RT_FAILURE(rc))
        return rc;
    if (!access(pszRealExec, X_OK))
        return VINF_SUCCESS;
    if (   errno == EACCES
        || errno == EPERM)
        return RTErrConvertFromErrno(errno);
    return VERR_TRY_AGAIN;
}


RTR3DECL(int)   RTProcCreateEx(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                               PCRTHANDLE phStdIn, PCRTHANDLE phStdOut, PCRTHANDLE phStdErr, const char *pszAsUser,
                               const char *pszPassword, void *pvExtraData, PRTPROCESS phProcess)
{
    int rc;
    LogFlow(("RTProcCreateEx: pszExec=%s pszAsUser=%s\n", pszExec, pszAsUser));

    /*
     * Input validation
     */
    AssertPtrReturn(pszExec, VERR_INVALID_POINTER);
    AssertReturn(*pszExec, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTPROC_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & RTPROC_FLAGS_DETACHED) || !phProcess, VERR_INVALID_PARAMETER);
    AssertReturn(hEnv != NIL_RTENV, VERR_INVALID_PARAMETER);
    AssertPtrReturn(papszArgs, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszAsUser, VERR_INVALID_POINTER);
    AssertReturn(!pszAsUser || *pszAsUser, VERR_INVALID_PARAMETER);
    AssertReturn(!pszPassword || pszAsUser, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszPassword, VERR_INVALID_POINTER);
#if defined(RT_OS_OS2)
    if (fFlags & RTPROC_FLAGS_DETACHED)
        return VERR_PROC_DETACH_NOT_SUPPORTED;
#endif
    AssertReturn(pvExtraData == NULL || (fFlags & RTPROC_FLAGS_DESIRED_SESSION_ID), VERR_INVALID_PARAMETER);

    /*
     * Get the file descriptors for the handles we've been passed.
     */
    PCRTHANDLE  paHandles[3] = { phStdIn, phStdOut, phStdErr };
    int         aStdFds[3]   = {      -1,       -1,       -1 };
    for (int i = 0; i < 3; i++)
    {
        if (paHandles[i])
        {
            AssertPtrReturn(paHandles[i], VERR_INVALID_POINTER);
            switch (paHandles[i]->enmType)
            {
                case RTHANDLETYPE_FILE:
                    aStdFds[i] = paHandles[i]->u.hFile != NIL_RTFILE
                               ? (int)RTFileToNative(paHandles[i]->u.hFile)
                               : -2 /* close it */;
                    break;

                case RTHANDLETYPE_PIPE:
                    aStdFds[i] = paHandles[i]->u.hPipe != NIL_RTPIPE
                               ? (int)RTPipeToNative(paHandles[i]->u.hPipe)
                               : -2 /* close it */;
                    break;

                case RTHANDLETYPE_SOCKET:
                    aStdFds[i] = paHandles[i]->u.hSocket != NIL_RTSOCKET
                               ? (int)RTSocketToNative(paHandles[i]->u.hSocket)
                               : -2 /* close it */;
                    break;

                default:
                    AssertMsgFailedReturn(("%d: %d\n", i, paHandles[i]->enmType), VERR_INVALID_PARAMETER);
            }
            /** @todo check the close-on-execness of these handles?  */
        }
    }

    for (int i = 0; i < 3; i++)
        if (aStdFds[i] == i)
            aStdFds[i] = -1;

    for (int i = 0; i < 3; i++)
        AssertMsgReturn(aStdFds[i] < 0 || aStdFds[i] > i,
                        ("%i := %i not possible because we're lazy\n", i, aStdFds[i]),
                        VERR_NOT_SUPPORTED);

    /*
     * Resolve the user id if specified.
     */
    uid_t uid = ~(uid_t)0;
    gid_t gid = ~(gid_t)0;
    if (pszAsUser)
    {
        rc = rtCheckCredentials(pszAsUser, pszPassword, &gid, &uid);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Create the child environment if either RTPROC_FLAGS_PROFILE or
     * RTPROC_FLAGS_ENV_CHANGE_RECORD are in effect.
     */
    RTENV hEnvToUse = hEnv;
    if (   (fFlags & (RTPROC_FLAGS_ENV_CHANGE_RECORD | RTPROC_FLAGS_PROFILE))
        && (   (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)
            || hEnv == RTENV_DEFAULT) )
    {
        if (fFlags & RTPROC_FLAGS_PROFILE)
            rc = rtProcPosixCreateProfileEnv(&hEnvToUse, pszAsUser, uid, gid, fFlags);
        else
            rc = RTEnvClone(&hEnvToUse, RTENV_DEFAULT);
        if (RT_FAILURE(rc))
            return rc;

        if ((fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD) && hEnv != RTENV_DEFAULT)
        {
            rc = RTEnvApplyChanges(hEnvToUse, hEnv);
            if (RT_FAILURE(rc))
            {
                RTEnvDestroy(hEnvToUse);
                return rc;
            }
        }
    }

    /*
     * Check for execute access to the file.
     */
    char szRealExec[RTPATH_MAX];
    if (access(pszExec, X_OK) == 0)
        rc = VINF_SUCCESS;
    else
    {
        rc = errno;
        if (   !(fFlags & RTPROC_FLAGS_SEARCH_PATH)
            || rc != ENOENT
            || RTPathHavePath(pszExec) )
            rc = RTErrConvertFromErrno(rc);
        else
        {
            /* search */
            char *pszPath = RTEnvDupEx(hEnvToUse, "PATH");
            rc = RTPathTraverseList(pszPath, ':', rtPathFindExec, (void *)pszExec, &szRealExec[0]);
            RTStrFree(pszPath);
            if (RT_SUCCESS(rc))
                pszExec = szRealExec;
            else
                rc = rc == VERR_END_OF_STRING ? VERR_FILE_NOT_FOUND : rc;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * The rest of the process creation is reused internally by
         * rtProcPosixCreateProfileEnv.
         */
        rc = rtProcPosixCreateInner(pszExec, papszArgs, hEnv, hEnvToUse, fFlags, pszAsUser, uid, gid,
                                    RT_ELEMENTS(aStdFds), aStdFds, phProcess);
    }
    if (hEnvToUse != hEnv)
        RTEnvDestroy(hEnvToUse);
    return rc;
}


/**
 * The inner 2nd half of RTProcCreateEx.
 *
 * This is also used by rtProcPosixCreateProfileEnv().
 *
 * @returns IPRT status code.
 * @param   pszExec     The executable to run (absolute path, X_OK).
 * @param   papszArgs   The arguments.
 * @param   hEnv        The original enviornment request, needed for adjustments
 *                      if starting as different user.
 * @param   hEnvToUse   The environment we should use.
 * @param   fFlags      The process creation flags, RTPROC_FLAGS_XXX.
 * @param   pszAsUser   The user to start the process as, if requested.
 * @param   uid         The UID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   gid         The GID corrsponding to @a pszAsUser, ~0 if NULL.
 * @param   cRedirFds   Number of redirection file descriptors.
 * @param   paRedirFds  Pointer to redirection file descriptors.  Entries
 *                      containing -1 are not modified (inherit from parent),
 *                      -2 indicates that the descriptor should be closed in the
 *                      child.
 * @param   phProcess   Where to return the process ID on success.
 */
static int rtProcPosixCreateInner(const char *pszExec, const char * const *papszArgs, RTENV hEnv, RTENV hEnvToUse,
                                  uint32_t fFlags, const char *pszAsUser, uid_t uid, gid_t gid,
                                  unsigned cRedirFds, int *paRedirFds, PRTPROCESS phProcess)
{
    /*
     * Get the environment block.
     */
    const char * const *papszEnv = RTEnvGetExecEnvP(hEnvToUse);
    AssertPtrReturn(papszEnv, VERR_INVALID_HANDLE);

    /*
     * Optimize the redirections.
     */
    while (cRedirFds > 0 && paRedirFds[cRedirFds - 1] == -1)
        cRedirFds--;

    /*
     * Child PID.
     */
    pid_t pid = -1;

    /*
     * Take care of detaching the process.
     *
     * HACK ALERT! Put the process into a new process group with pgid = pid
     * to make sure it differs from that of the parent process to ensure that
     * the IPRT waitpid call doesn't race anyone (read XPCOM) doing group wide
     * waits. setsid() includes the setpgid() functionality.
     * 2010-10-11 XPCOM no longer waits for anything, but it cannot hurt.
     */
#ifndef RT_OS_OS2
    if (fFlags & RTPROC_FLAGS_DETACHED)
    {
# ifdef RT_OS_SOLARIS
        int templateFd = -1;
        if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
        {
            templateFd = rtSolarisContractPreFork();
            if (templateFd == -1)
                return VERR_OPEN_FAILED;
        }
# endif /* RT_OS_SOLARIS */
        pid = fork();
        if (!pid)
        {
# ifdef RT_OS_SOLARIS
            if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
                rtSolarisContractPostForkChild(templateFd);
# endif
            setsid(); /* see comment above */

            pid = -1;
            /* Child falls through to the actual spawn code below. */
        }
        else
        {
# ifdef RT_OS_SOLARIS
            if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
                rtSolarisContractPostForkParent(templateFd, pid);
# endif
            if (pid > 0)
            {
                /* Must wait for the temporary process to avoid a zombie. */
                int status = 0;
                pid_t pidChild = 0;

                /* Restart if we get interrupted. */
                do
                {
                    pidChild = waitpid(pid, &status, 0);
                } while (   pidChild == -1
                         && errno == EINTR);

                /* Assume that something wasn't found. No detailed info. */
                if (status)
                    return VERR_PROCESS_NOT_FOUND;
                if (phProcess)
                    *phProcess = 0;
                return VINF_SUCCESS;
            }
            return RTErrConvertFromErrno(errno);
        }
    }
#endif

    /*
     * Spawn the child.
     *
     * Any spawn code MUST not execute any atexit functions if it is for a
     * detached process. It would lead to running the atexit functions which
     * make only sense for the parent. libORBit e.g. gets confused by multiple
     * execution. Remember, there was only a fork() so far, and until exec()
     * is successfully run there is nothing which would prevent doing anything
     * silly with the (duplicated) file descriptors.
     */
    int rc;
#ifdef HAVE_POSIX_SPAWN
    /** @todo OS/2: implement DETACHED (BACKGROUND stuff), see VbglR3Daemonize.  */
    if (   uid == ~(uid_t)0
        && gid == ~(gid_t)0)
    {
        /* Spawn attributes. */
        posix_spawnattr_t Attr;
        rc = posix_spawnattr_init(&Attr);
        if (!rc)
        {
            /* Indicate that process group and signal mask are to be changed,
               and that the child should use default signal actions. */
            rc = posix_spawnattr_setflags(&Attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
            Assert(rc == 0);

            /* The child starts in its own process group. */
            if (!rc)
            {
                rc = posix_spawnattr_setpgroup(&Attr, 0 /* pg == child pid */);
                Assert(rc == 0);
            }

            /* Unmask all signals. */
            if (!rc)
            {
                sigset_t SigMask;
                sigemptyset(&SigMask);
                rc = posix_spawnattr_setsigmask(&Attr, &SigMask); Assert(rc == 0);
            }

            /* File changes. */
            posix_spawn_file_actions_t  FileActions;
            posix_spawn_file_actions_t *pFileActions = NULL;
            if (!rc && cRedirFds > 0)
            {
                rc = posix_spawn_file_actions_init(&FileActions);
                if (!rc)
                {
                    pFileActions = &FileActions;
                    for (unsigned i = 0; i < cRedirFds; i++)
                    {
                        int fd = paRedirFds[i];
                        if (fd == -2)
                            rc = posix_spawn_file_actions_addclose(&FileActions, i);
                        else if (fd >= 0 && fd != (int)i)
                        {
                            rc = posix_spawn_file_actions_adddup2(&FileActions, fd, i);
                            if (!rc)
                            {
                                for (unsigned j = i + 1; j < cRedirFds; j++)
                                    if (paRedirFds[j] == fd)
                                    {
                                        fd = -1;
                                        break;
                                    }
                                if (fd >= 0)
                                    rc = posix_spawn_file_actions_addclose(&FileActions, fd);
                            }
                        }
                        if (rc)
                            break;
                    }
                }
            }

            if (!rc)
                rc = posix_spawn(&pid, pszExec, pFileActions, &Attr, (char * const *)papszArgs,
                                 (char * const *)papszEnv);

            /* cleanup */
            int rc2 = posix_spawnattr_destroy(&Attr); Assert(rc2 == 0); NOREF(rc2);
            if (pFileActions)
            {
                rc2 = posix_spawn_file_actions_destroy(pFileActions);
                Assert(rc2 == 0);
            }

            /* return on success.*/
            if (!rc)
            {
                /* For a detached process this happens in the temp process, so
                 * it's not worth doing anything as this process must exit. */
                if (fFlags & RTPROC_FLAGS_DETACHED)
                    _Exit(0);
                if (phProcess)
                    *phProcess = pid;
                return VINF_SUCCESS;
            }
        }
        /* For a detached process this happens in the temp process, so
         * it's not worth doing anything as this process must exit. */
        if (fFlags & RTPROC_FLAGS_DETACHED)
            _Exit(124);
    }
    else
#endif
    {
#ifdef RT_OS_SOLARIS
        int templateFd = -1;
        if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
        {
            templateFd = rtSolarisContractPreFork();
            if (templateFd == -1)
                return VERR_OPEN_FAILED;
        }
#endif /* RT_OS_SOLARIS */
        pid = fork();
        if (!pid)
        {
#ifdef RT_OS_SOLARIS
            if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
                rtSolarisContractPostForkChild(templateFd);
#endif /* RT_OS_SOLARIS */
            if (!(fFlags & RTPROC_FLAGS_DETACHED))
                setpgid(0, 0); /* see comment above */

            /*
             * Change group and user if requested.
             */
#if 1 /** @todo This needs more work, see suplib/hardening. */
            if (pszAsUser)
            {
                int ret = initgroups(pszAsUser, gid);
                if (ret)
                {
                    if (fFlags & RTPROC_FLAGS_DETACHED)
                        _Exit(126);
                    else
                        exit(126);
                }
            }
            if (gid != ~(gid_t)0)
            {
                if (setgid(gid))
                {
                    if (fFlags & RTPROC_FLAGS_DETACHED)
                        _Exit(126);
                    else
                        exit(126);
                }
            }

            if (uid != ~(uid_t)0)
            {
                if (setuid(uid))
                {
                    if (fFlags & RTPROC_FLAGS_DETACHED)
                        _Exit(126);
                    else
                        exit(126);
                }
            }
#endif

            /*
             * Some final profile environment tweaks, if running as user.
             */
            if (   (fFlags & RTPROC_FLAGS_PROFILE)
                && pszAsUser
                && (   (fFlags & RTPROC_FLAGS_ENV_CHANGE_RECORD)
                    || hEnv == RTENV_DEFAULT) )
            {
                rc = rtProcPosixAdjustProfileEnvFromChild(hEnvToUse, fFlags, hEnv);
                papszEnv = RTEnvGetExecEnvP(hEnvToUse);
                if (RT_FAILURE(rc) || !papszEnv)
                {
                    if (fFlags & RTPROC_FLAGS_DETACHED)
                        _Exit(126);
                    else
                        exit(126);
                }
            }

            /*
             * Unset the signal mask.
             */
            sigset_t SigMask;
            sigemptyset(&SigMask);
            rc = sigprocmask(SIG_SETMASK, &SigMask, NULL);
            Assert(rc == 0);

            /*
             * Apply changes to the standard file descriptor and stuff.
             */
            for (unsigned i = 0; i < cRedirFds; i++)
            {
                int fd = paRedirFds[i];
                if (fd == -2)
                    close(i);
                else if (fd >= 0)
                {
                    int rc2 = dup2(fd, i);
                    if (rc2 != (int)i)
                    {
                        if (fFlags & RTPROC_FLAGS_DETACHED)
                            _Exit(125);
                        else
                            exit(125);
                    }
                    for (unsigned j = i + 1; j < cRedirFds; j++)
                        if (paRedirFds[j] == fd)
                        {
                            fd = -1;
                            break;
                        }
                    if (fd >= 0)
                        close(fd);
                }
            }

            /*
             * Finally, execute the requested program.
             */
            rc = execve(pszExec, (char * const *)papszArgs, (char * const *)papszEnv);
            if (errno == ENOEXEC)
            {
                /* This can happen when trying to start a shell script without the magic #!/bin/sh */
                RTAssertMsg2Weak("Cannot execute this binary format!\n");
            }
            else
                RTAssertMsg2Weak("execve returns %d errno=%d\n", rc, errno);
            RTAssertReleasePanic();
            if (fFlags & RTPROC_FLAGS_DETACHED)
                _Exit(127);
            else
                exit(127);
        }
#ifdef RT_OS_SOLARIS
        if (!(fFlags & RTPROC_FLAGS_SAME_CONTRACT))
            rtSolarisContractPostForkParent(templateFd, pid);
#endif /* RT_OS_SOLARIS */
        if (pid > 0)
        {
            /* For a detached process this happens in the temp process, so
             * it's not worth doing anything as this process must exit. */
            if (fFlags & RTPROC_FLAGS_DETACHED)
                _Exit(0);
            if (phProcess)
                *phProcess = pid;
            return VINF_SUCCESS;
        }
        /* For a detached process this happens in the temp process, so
         * it's not worth doing anything as this process must exit. */
        if (fFlags & RTPROC_FLAGS_DETACHED)
            _Exit(124);
        return RTErrConvertFromErrno(errno);
    }

    return VERR_NOT_IMPLEMENTED;
}


RTR3DECL(int)   RTProcDaemonizeUsingFork(bool fNoChDir, bool fNoClose, const char *pszPidfile)
{
    /*
     * Fork the child process in a new session and quit the parent.
     *
     * - fork once and create a new session (setsid). This will detach us
     *   from the controlling tty meaning that we won't receive the SIGHUP
     *   (or any other signal) sent to that session.
     * - The SIGHUP signal is ignored because the session/parent may throw
     *   us one before we get to the setsid.
     * - When the parent exit(0) we will become an orphan and re-parented to
     *   the init process.
     * - Because of the sometimes unexpected semantics of assigning the
     *   controlling tty automagically when a session leader first opens a tty,
     *   we will fork() once more to get rid of the session leadership role.
     */

    /* We start off by opening the pidfile, so that we can fail straight away
     * if it already exists. */
    int fdPidfile = -1;
    if (pszPidfile != NULL)
    {
        /* @note the exclusive create is not guaranteed on all file
         * systems (e.g. NFSv2) */
        if ((fdPidfile = open(pszPidfile, O_RDWR | O_CREAT | O_EXCL, 0644)) == -1)
            return RTErrConvertFromErrno(errno);
    }

    /* Ignore SIGHUP straight away. */
    struct sigaction OldSigAct;
    struct sigaction SigAct;
    memset(&SigAct, 0, sizeof(SigAct));
    SigAct.sa_handler = SIG_IGN;
    int rcSigAct = sigaction(SIGHUP, &SigAct, &OldSigAct);

    /* First fork, to become independent process. */
    pid_t pid = fork();
    if (pid == -1)
    {
        if (fdPidfile != -1)
            close(fdPidfile);
        return RTErrConvertFromErrno(errno);
    }
    if (pid != 0)
    {
        /* Parent exits, no longer necessary. The child gets reparented
         * to the init process. */
        exit(0);
    }

    /* Create new session, fix up the standard file descriptors and the
     * current working directory. */
    /** @todo r=klaus the webservice uses this function and assumes that the
     * contract id of the daemon is the same as that of the original process.
     * Whenever this code is changed this must still remain possible. */
    pid_t newpgid = setsid();
    int SavedErrno = errno;
    if (rcSigAct != -1)
        sigaction(SIGHUP, &OldSigAct, NULL);
    if (newpgid == -1)
    {
        if (fdPidfile != -1)
            close(fdPidfile);
        return RTErrConvertFromErrno(SavedErrno);
    }

    if (!fNoClose)
    {
        /* Open stdin(0), stdout(1) and stderr(2) as /dev/null. */
        int fd = open("/dev/null", O_RDWR);
        if (fd == -1) /* paranoia */
        {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            fd = open("/dev/null", O_RDWR);
        }
        if (fd != -1)
        {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
    }

    if (!fNoChDir)
    {
        int rcIgnored = chdir("/");
        NOREF(rcIgnored);
    }

    /* Second fork to lose session leader status. */
    pid = fork();
    if (pid == -1)
    {
        if (fdPidfile != -1)
            close(fdPidfile);
        return RTErrConvertFromErrno(errno);
    }

    if (pid != 0)
    {
        /* Write the pid file, this is done in the parent, before exiting. */
        if (fdPidfile != -1)
        {
            char szBuf[256];
            size_t cbPid = RTStrPrintf(szBuf, sizeof(szBuf), "%d\n", pid);
            ssize_t cbIgnored = write(fdPidfile, szBuf, cbPid); NOREF(cbIgnored);
            close(fdPidfile);
        }
        exit(0);
    }

    if (fdPidfile != -1)
        close(fdPidfile);

    return VINF_SUCCESS;
}

