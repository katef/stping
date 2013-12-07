# $Id$

.if !defined(SRCDIRPEFIX)
. if !defined(_SRCDIRPREFIX_RELATIVE)
SRCDIRPREFIX!=	cd ${.CURDIR} && pwd
. else
SRCDIRPREFIX!=	cd ${_SRCDIRPREFIX_RELATIVE} && pwd
. endif
.endif

.MAKEFLAGS:	-m ${SRCDIRPREFIX}/mk -r

