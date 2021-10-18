# $Id$
## @file
# IPRT - SED script for generating a list of assembly files for make inclusion.
#

#
# Copyright (C) 2012-2021 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL) only, as it comes in the "COPYING.CDDL" file of the
# VirtualBox OSE distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#

# Header and footer.
1b header
$b footer

# Drop all lines from the start of the file until the SED: START marker.
1,/SED: START/d

# Drop all lines from the SED: END marker and till the end of the file.
/SED: END/,$d

# We are only interested in the SUPEXP_STK_BACK lines.
/^ *SUPEXP_STK_BACKF*(/!d
s/^ *SUPEXP_STK_BACKF*( *[0-9][0-9]* *, *\([^)][^)]*\)),.*$*/    \$(SUPR0_0_OUTDIR)\/StkBack_\1.asm \\/
b end

:header
i\#
i\# Autogenerated. DO NOT EDIT!
i\#
i\SUPR0_VBOX_FILES = \\
d


:footer
i
i\# The end.
i
d

:end
