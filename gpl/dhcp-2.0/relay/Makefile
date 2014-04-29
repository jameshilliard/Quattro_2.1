
ISM_DEPTH       := ./../..
include $(ISM_DEPTH)/ismdefs

# Makefile.conf
#
# Copyright (c) 1996, 1997, 1998, 1999 The Internet Software Consortium.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of The Internet Software Consortium nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
# CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
# BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
# THE INTERNET SOFTWARE CONSORTIUM OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
#

## Defaults...
SCRIPT = none
BINDIR = /usr/sbin
CLIENTBINDIR=/sbin
ADMMANEXT = .0
FFMANEXT = .0
ADMMANDIR = /usr/share/man/cat8
FFMANDIR = /usr/share/man/cat5
INSTALL = install -c
MANINSTALL = install -c -m 444
CHMOD = chmod
CATMANPAGES =
MANCAT = cat
ETC = /etc
VARRUN = /var/run
VARDB = /var/db

## Linux 2.2
#--linux-2.2--
#COPTS = -DLINUX_MAJOR=2 -DLINUX_MINOR=2
LCOPTS = -DLINUX_MAJOR=2 -DLINUX_MINOR=2
CF = cf/linux.h
ADMMANDIR = /usr/man/man8
ADMMANEXT = .8
FFMANDIR = /usr/share/man/man5
FFMANEXT = .5
MANCAT = man
VARRUN = /var/run
VARDB = /var/state/dhcp	# see rationale in includes/cf/linux.h
SCRIPT=linux
#--linux-2.2--

CATMANPAGES = dhcrelay.cat8
SEDMANPAGES = dhcrelay.cat8
SRCS   = dhcrelay.c
OBJS   = dhcrelay.o
PROG   = dhcrelay
MAN    = dhcrelay.8

#DEBUG  = -g
INCLUDES = -I.. -I../includes
DHCPLIB = ../common/libdhcp.a
#CFLAGS = $(DEBUG) $(PREDEFINES) $(INCLUDES) $(COPTS)
LCDEFS = $(PREDEFINES)
LCINCS = $(INCLUDES)
LCWARN =  \
        -Wno-all                  \
        -Wno-cast-qual            \
        -Wno-conversion           \
        -Wno-implicit             \
        -Wno-missing-prototypes   \
        -Wno-strict-prototypes    \
        -Wno-undef                \
        -Wno-missing-declarations \
        -Wno-sign-compare         \
        -Wno-unused               \
        -Wno-error                \
        $(NULL)
#
# Removed because -Wno-tivo is not supported in gcc 4.2.0
#        -Wno-tivo                 \
#

default: all

clobber: clean
	-rm -f $(PROG) *.d

all:	$(PROG)

clean:
	-rm -f $(OBJS) dhclient.o
	
realclean: clean
	-rm -f $(PROG) $(CATMANPAGES) $(SEDMANPAGES) *~ #*

distclean: realclean
	-rm -f Makefile

# These should only be done on 4.4 BSD-based systems, since the mandoc
# macros aren't available on older unices.   Catted man pages are
# provided in the distribution so that this doesn't become a problem.

dhcrelay.cat8:	dhcrelay.man8
	nroff -man dhcrelay.man8 >dhcrelay.cat8

dhcrelay.man8:	dhcrelay.8
	sed -e "s#ETCDIR#$(ETC)#" -e "s#DBDIR#$(VARDB)#" \
		-e "s#RUNDIR#$(VARRUN)#" < dhcrelay.8 >dhcrelay.man8

dhcrelay:	dhcrelay.o $(DHCPLIB)
	$(CCF) -o $(PROG) dhcrelay.o $(DHCPLIB) $(LIBS) $(LDFLAGS)
#	$(CC) $(LFLAGS) -o $(PROG) dhcrelay.o $(DHCPLIB) $(LIBS)
