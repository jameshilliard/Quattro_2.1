AM_CPPFLAGS=-I ${top_srcdir}/libop -I ${top_srcdir}/libutil
AM_CFLAGS = @OP_CFLAGS@

LIBS=@POPT_LIBS@ @LIBERTY_LIBS@

bin_PROGRAMS = ophelp


bin_SCRIPTS = opcontrol
CLEANFILES = $(bin_SCRIPTS)
dist_bin_SCRIPTS = opcontrol.in objdump
EXTRA_DIST = $(dist_bin_SCRIPTS)

do_subst = sed \
	-e 's,[@]OP_MNTDIR[@],${MNTDIR},g' \
	-e 's,[@]OP_CONFIGDIR[@],${CONFIGDIR},g' \
	-e 's,[@]OP_DATADIR[@],${DATADIR},g'

opcontrol: opcontrol.in Makefile
	$(do_subst) < $(srcdir)/opcontrol.in > opcontrol
	chmod +x opcontrol

ophelp_SOURCES = ophelp.c
ophelp_LDADD = ../libop/libop.a ../libutil/libutil.a
