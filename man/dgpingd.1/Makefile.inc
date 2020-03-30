# generic Makefile.inc

.if defined(_SRCDIRPREFIX_RELATIVE)
_SRCDIRPREFIX_RELATIVE := ${_SRCDIRPREFIX_RELATIVE}/..
.else
_SRCDIRPREFIX_RELATIVE = ..
.endif

.include "../Makefile.inc"

