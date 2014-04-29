#
# File: Makefile
#
# Copyright 2010 TiVo Inc.  All rights reserved.
#
###############################################################################

ISM_DEPTH       := ./../..
include $(ISM_DEPTH)/ismdefs

#
# lib/Makefile	Makefile for the net-lib function collection
#
# NET-LIB	A collection of functions used from the base set of the
#		NET-2 Networking Distribution for the LINUX operating
#		system.  (net-tools, net-drivers)
#
# Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
#		Copyright 1993 MicroWalt Corporation
#
#		This program is free software; you can redistribute it
#		and/or  modify it under  the terms of  the GNU General
#		Public  License as  published  by  the  Free  Software
#		Foundation;  either  version 2 of the License, or  (at
#		your option) any later version.
#

LCDEFS =
LCINCS = -I.. -I../include

LCWARN = \
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

#HWFILES   = \
        hw.c loopback.c slip.c ether.c ax25.c ppp.c arcnet.c tr.c tunnel.c \
        frame.c sit.c rose.c ash.c fddi.c hippi.c hdlclapb.c strip.c \
        irda.c ec_hw.c x25.c
#AFFILES   = \
        unix.c inet.c inet6.c ax25.c ipx.c ddp.c ipx.c netrom.c af.c rose.c \
        econet.c x25.c
#AFGRFILES = inet_gr.c inet6_gr.c ipx_gr.c ddp_gr.c netrom_gr.c ax25_gr.c \
        rose_gr.c getroute.c x25_gr.c
#AFSRFILES = inet_sr.c inet6_sr.c netrom_sr.c ipx_sr.c setroute.c x25_sr.c
#ACTFILES  = slip_ac.c ppp_ac.c activate.c
#VARIA     = getargs.c masq_info.c proc.c util.c nstrcmp.c interface.c \
        sockets.c util-ank.c

HWFILES   = hw.c loopback.c ether.c ppp.c
AFFILES   = unix.c inet.c af.c
AFGRFILES = inet_gr.c getroute.c
AFSRFILES = inet_sr.c setroute.c
ACTFILES  =
VARIA     = getargs.c proc.c util.c nstrcmp.c interface.c sockets.c

SFILES = \
        $(HWFILES) $(AFFILES) $(AFGRFILES) $(AFSRFILES) \
        $(ACTFILES) $(VARIA) \

LIBS = net-tools.a

include $(ISMRULES)

