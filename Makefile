CFLAGS += -std=c99 -g -pedantic -Wall -Os -I/usr/X11R6/include
LDFLAGS += -lxcb -L/usr/X11R6/lib
PROG = wm
SRC =  ${PROG}.c
OBJ =  ${SRC:.c=.o}
RM ?= /bin/rm
PREFIX ?= /usr

all: ${PROG}

.c.o: ${SRC}
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}:

${PROG}: ${OBJ}
	@echo LD $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean: ${OBJ} ${PROG}
	${RM} ${OBJ} ${PROG}

install: ${PROG}
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}

uninstall: ${DESTDIR}${PREFIX}/bin/${PROG}
	${RM} ${DESTDIR}${PREFIX}/bin/${PROG}
