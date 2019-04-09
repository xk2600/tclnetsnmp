.SUFFIXES: .tcl .tclm4

PACKAGE= tclnetsnmp
PACKAGE_VERSION= 3.0
DESTDIR= /usr/local/lib/${PACKAGE}-${PACKAGE_VERSION}
TCLVERSION= 8.6
TCLSH= /usr/local/bin/tclsh

SRCS+= tclnetsnmp.c
LIBRARY= tclnetsnmp.so

# Сконструируем имя Xxx_Init функции, как tcl её ожидает увидеть.
# Т.е. если package файл "abs.so", то имя функции в C будет Abc_Init().
PACKAGE_INIT!= echo "puts [string totitle ${LIBRARY:R}]_Init" | tclsh

# Нужно включать одновременно и для libnetsnmp, потому что некоторые поля
# struct netsnmp_session (например community) используют память выделенную в
# tclnetsnmp.c, а освобождается она в библиотеке libnetsnmp.so.X. Т.е.
# libnetsnmp.so:free() будет пытаться освободить выделенную в нашем модуле 
# dmalloc-ом память, что плохо.
#DMALLOC= 

TCLM4SRCS+= tclnetsnmp.tclm4
TCLGENSRCS+= tclnetsnmp.tcl
TCLSRCS+= ${TCLGENSRCS}

CC= cc
CFLAGS+= -std=c99
CFLAGS+= -g -DWITH_DEBUGLOG
.ifdef DMALLOC
CFLAGS+= -DDMALLOC -DDMALLOC_FUNC_CHECK
.endif
CFLAGS+= -I/usr/local/include -I/usr/local/include/tcl${TCLVERSION}
LDFLAGS+= -L/usr/local/lib 

SNMP_CFLAGS!= net-snmp-config --cflags
SNMP_LDFLAGS!= net-snmp-config --libdir
SNMP_LDADD!= net-snmp-config --libs
CFLAGS+= ${SNMP_CFLAGS}
LDFLAGS+= ${SNMP_LDFLAGS}
LDADD+= ${SNMP_LDADD}

LDADD+= -lfoosnmpex -lfootcl -lfoorbtree -lfoomem -lfoopthread -lfoolog -ltcl${TCLVERSION:C/\.//}
.ifdef DMALLOC
LDADD+= -ldmalloc
.endif
LDADD+= -lpthread

CFLAGS+= -DPACKAGE=${PACKAGE} -DPACKAGE_VERSION=${PACKAGE_VERSION} -DTCLVERSION=${TCLVERSION} -DPACKAGE_INIT=${PACKAGE_INIT}

MAN= tclnetsnmp.n
MANDIR= /usr/local/man
M4= gm4

all		: ${LIBRARY} ${TCLSRCS} .PHONY

${LIBRARY}	: ${SRCS}
	${CC} -shared -fpic -DPIC -Wl,-x ${CFLAGS} ${LDFLAGS} ${.ALLSRC} ${LDADD} -o ${.TARGET}

install-bin	: ${LIBRARY} ${TCLSRCS} .PHONY
	mkdir -p ${DESTDIR}
	install -v ${.ALLSRC} ${DESTDIR}
	echo "pkg_mkIndex -verbose ${DESTDIR} *.tcl *.so" | ${TCLSH}

.for f in ${MAN}
${MANDIR}/man${f:E}/${f}.gz	: ${f}
	@install -v -m 444 ${.ALLSRC} ${.TARGET:R}
	@gzip -9f ${.TARGET:R}

MANTARGETS+= ${MANDIR}/man${f:E}/$f.gz
.endfor

install-man	: ${MANTARGETS} .PHONY

install		: install-bin install-man .PHONY

clean		: .PHONY
	-@unlink ${LIBRARY}
	-@rm ${TCLGENSRCS}

.tclm4.tcl	:
	${M4} -P -D M4_PACKAGE=${PACKAGE} -D M4_PACKAGE_VERSION=${PACKAGE_VERSION} ${.IMPSRC} > ${.TARGET}
