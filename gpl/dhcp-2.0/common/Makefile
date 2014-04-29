
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

CATMANPAGES = dhcp-options.cat5
SEDMANPAGES = dhcp-options.man5
SRC    = raw.c parse.c nit.c icmp.c dispatch.c conflex.c upf.c bpf.c socket.c \
       	 lpf.c packet.c memory.c print.c options.c inet.c convert.c \
	 tree.c tables.c hash.c alloc.c errwarn.c inet_addr.c dlpi.c \
	 tr.c ethernet.c
OBJ    = raw.o parse.o nit.o icmp.o dispatch.o conflex.o upf.o bpf.o socket.o \
	 lpf.o packet.o memory.o print.o options.o inet.o convert.o \
	 tree.o tables.o hash.o alloc.o errwarn.o inet_addr.o dlpi.o \
	 tr.o ethernet.o
MAN    = dhcp-options.5

#DEBUG  = -g
INCLUDES = -I.. -I../includes
#CFLAGS = $(DEBUG) $(PREDEFINES) $(INCLUDES) $(COPTS)
LCDEFS = $(PREDEFINES) $(BOOTARCHES)
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
	-rm -f libdhcp.a *.d

all:	libdhcp.a

libdhcp.a:	$(OBJ)
	rm -f libdhcp.a
	$(AR) cruv libdhcp.a $(OBJ)
	$(RANLIB) libdhcp.a

clean:
	-rm -f $(OBJ)
	
realclean: clean
	-rm -f libdhcp.a *~ #* $(CATMANPAGES) $(SEDMANPAGES)

distclean: realclean
	-rm -f Makefile

dhcp-options.cat5:	dhcp-options.man5
	nroff -man dhcp-options.man5 >dhcp-options.cat5

dhcp-options.man5:	dhcp-options.5
	sed -e "s#ETCDIR#$(ETC)#g" -e "s#DBDIR#$(VARDB)#g" \
		-e "s#RUNDIR#$(VARRUN)#g" < dhcp-options.5 >dhcp-options.man5
