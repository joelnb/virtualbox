# $Id$
## @file
# WebService - SED script for extracting inline functions from soapH.h
#              for putting them in a C++ source file.
#

#
# Copyright (C) 2020-2022 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

/^inline /,/^}/ {
    /^inline /,/^{/ {
        s/^inline/\n\/\*noinline\*\//
        s/ *= *\([^,)]*\)\([),]\)/ \/\*=\1\*\/\2/
    }
    p
}
1 c #include "soapH.h"
d

