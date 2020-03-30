.include "../../share/mk/top.mk"

SRC += src/dgping.c src/dgpingd.c
SRC += src/stping.c src/stpingd.c
SRC += src/common.c

.for src in ${SRC:M*.c}
CFLAGS.${src} += -I src
.endfor

PROG += dgping dgpingd
PROG += stping stpingd

LFLAGS.dgping += -lm
LFLAGS.stping += -lm

${BUILD}/bin/dgping:  ${BUILD}/src/dgping.o  ${BUILD}/src/common.o
${BUILD}/bin/stping:  ${BUILD}/src/stping.o  ${BUILD}/src/common.o

${BUILD}/bin/dgpingd: ${BUILD}/src/dgpingd.o ${BUILD}/src/common.o
${BUILD}/bin/stpingd: ${BUILD}/src/stpingd.o ${BUILD}/src/common.o

