CFLAGS += -std=c99 -g -pedantic -Wall -Os -I/usr/X11R6/include
LDFLAGS	+= -lxcb -L/usr/X11R6/lib
SRC	=  wm.c
OBJ	=  ${SRC:.c=.o}
RM	?= /bin/rm
PREFIX	?= /usr

all: wm

.c.o: ${SRC}
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h

wm: ${OBJ}
	@echo LD $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean: ${OBJ} wm
	${RM} ${OBJ} wm

install: wm
	install -m 755 wm ${DESTDIR}${PREFIX}/bin/wm

uninstall: ${DESTDIR}${PREFIX}/bin/wm
	${RM} ${DESTDIR}${PREFIX}/bin/wm
