/* $Id$ */
/** @file
 * Stubs for libasound.
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

#ifndef VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubs_h
#define VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN
extern int audioLoadAlsaLib(void);

#ifndef SND_PCM_NO_SOFTVOL  /* probably not a good indicator ... */
extern int  snd_pcm_avail_delay(snd_pcm_t *, snd_pcm_sframes_t *, snd_pcm_sframes_t *);
#endif

#ifndef SND_CTL_TLVT_CHMAP_FIXED /* probably not a good indicator ... */
extern int  snd_device_name_hint(int, const char *, void ***);
extern int  snd_device_name_free_hint(void **);
extern char *snd_device_name_get_hint(const void *, const char *);
#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubs_h */

