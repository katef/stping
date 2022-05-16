.MAKEFLAGS: -r -m share/mk

# targets
all::  mkdir .WAIT dep prog
dep::
gen::
test:: all
install:: all
uninstall::
clean::

# things to override
CC     ?= gcc
BUILD  ?= build
PREFIX ?= /usr/local

# ${unix} is an arbitrary variable set by sys.mk
.if defined(unix)
.BEGIN::
	@echo "We don't use sys.mk; run ${MAKE} with -r" >&2
	@false
.endif

# layout
SUBDIR += src

.include <subdir.mk>
.include <obj.mk>
.include <dep.mk>
.include <ar.mk>
.include <prog.mk>
.include <mkdir.mk>
.include <install.mk>
.include <clean.mk>

# wasmtime isn't packaged for github's ubuntu, so we can't run this in CI there
.if !${CC:T:Memcc}

test:: ${BUILD}/bin/dgping ${BUILD}/bin/dgpingd
	${BUILD}/bin/dgpingd 127.0.0.1 9876 & echo $$! > /tmp/dgping.${.MAKE.PID}; sleep 1
	${BUILD}/bin/dgping -c 3 -i 0.1 127.0.0.1 9876
	kill $$(cat /tmp/dgping.${.MAKE.PID})
	rm /tmp/dgping.${.MAKE.PID}

test:: ${BUILD}/bin/stping ${BUILD}/bin/stpingd
	${BUILD}/bin/stpingd 127.0.0.1 9877 & echo $$! > /tmp/stping.${.MAKE.PID}; sleep 1
	${BUILD}/bin/stping -c 3 -i 0.1 127.0.0.1 9877
	kill $$(cat /tmp/stping.${.MAKE.PID})
	rm /tmp/stping.${.MAKE.PID}

.endif

