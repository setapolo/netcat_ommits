PREFIX ?= usr/local
PKGMANDIR ?= man
LDFLAGS += -lresolv

all:
	${CC} -o nc_opt_l ${LDFLAGS} netcat.c atomicio.c 

install:
	install -d ${DESTDIR}/${PREFIX}/bin/
	install nc_opt_l ${DESTDIR}/${PREFIX}/bin/nc_opt_l
	install -d ${DESTDIR}/${PREFIX}/${PKGMANDIR}/${PKGMANDIR}1/
	install -m 0644 nc_opt_l.1 ${DESTDIR}/${PREFIX}/${PKGMANDIR}/${PKGMANDIR}1/nc_opt_l.1
