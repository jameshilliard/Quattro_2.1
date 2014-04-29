#
# File: Makefile
#
# Copyright 2010 TiVo Inc.  All rights reserved.
#
###############################################################################

ISM_DEPTH       := ./..
include $(ISM_DEPTH)/ismdefs

#
# Makefile	Main Makefile for the net-tools Package
#
# NET-TOOLS	A collection of programs that form the base set of the
#		NET-3 Networking Distribution for the LINUX operating
#		system.
#
# Version:	2001-02-13
#
# Author:	Bernd Eckenfels <net-tools@lina.inka.de>
#		Copyright 1995-1996 Bernd Eckenfels, Germany
#
# URLs:		ftp://ftp.inka.de/pub/comp/Linux/networking/NetTools/ 
#		ftp://ftp.linux.org.uk/pub/linux/Networking/PROGRAMS/NetTools/
#		http://www.inka.de/sites/lina/linux/NetTools/index_en.html
#
# Based on:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
#		Copyright 1988-1993 MicroWalt Corporation
#
# Modifications:
#		Extensively modified from 01/21/94 onwards by
#		Alan Cox <A.Cox@swansea.ac.uk>
#		Copyright 1993-1994 Swansea University Computer Society
#
# Be careful! 
# This Makefile doesn't describe complete dependencies for all include files.
# If you change include files you might need to do make clean. 
#
#	{1.20}	Bernd Eckenfels:	Even more modifications for the new 
#					package layout
#	{1.21}	Bernd Eckenfels:	Check if config.in is newer than 
#					config.status
#	{1.22}  Bernd Eckenfels:	Include ypdomainname and nisdomainame
#
#	1.3.50-BETA6 private Release
#				
#960125	{1.23}	Bernd Eckenfels:	Peter Tobias' rewrite for 
#					makefile-based installation
#	1.3.50-BETA6a private Release
#
#960201 {1.24}	Bernd Eckenfels:	net-features.h added
#
#960201 1.3.50-BETA6b private Release
#
#960203 1.3.50-BETA6c private Release
#
#960204 1.3.50-BETA6d private Release
#
#960204 {1.25}	Bernd Eckenfels:	DISTRIBUTION added
#
#960205 1.3.50-BETA6e private Release
#
#960206	{1.26}	Bernd Eckenfels:	afrt.o removed (cleaner solution)
#
#960215 1.3.50-BETA6f Release
#
#960216 {1.30}	Bernd Eckenfels:	net-lib support
#960322 {1.31}	Bernd Eckenfels:	moveable netlib, TOPDIR
#960424 {1.32}	Bernd Eckenfels:	included the URLs in the Comment
#
#960514 1.31-alpha release
#
#960518 {1.33}	Bernd Eckenfels:	-I/usr/src/linux/include comment added
#
#	This program is free software; you can redistribute it
#	and/or  modify it under  the terms of  the GNU General
#	Public  License as  published  by  the  Free  Software
#	Foundation;  either  version 2 of the License, or  (at
#	your option) any later version.
#

ifeq ($(BUILD_TARGET),subdir)
DIRS = lib
endif

LCDEFS =
LCINCS = -I. -Iinclude -Ilib

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
#	-Wno-tivo                 \
#

ifeq ($(BUILD_TARGET),hostname)
PROGRAMS = hostname
endif

ifeq ($(BUILD_TARGET),netstat)
LLDLIBS = lib/net-tools.a
SFILES_LLDOBJS = statistics.c
PROGRAMS = netstat
endif

ifeq ($(BUILD_TARGET),therest)
LLDLIBS = lib/net-tools.a
PROGRAMS = ifconfig route arp rarp
endif

ifndef BUILD_TARGET
OTHERPROGS = recursedefault
endif

include $(ISMRULES)

.PHONY: recursedefault recurseclobber
recursedefault:
	$(MAKE) default BUILD_TARGET=subdir
	$(MAKE) default BUILD_TARGET=hostname
	$(MAKE) default BUILD_TARGET=netstat
	$(MAKE) default BUILD_TARGET=therest

ifndef BUILD_TARGET
clobber: recurseclobber
endif

recurseclobber:
	$(MAKE) clobber BUILD_TARGET=subdir
	$(MAKE) clobber BUILD_TARGET=hostname
	$(MAKE) clobber BUILD_TARGET=netstat
	$(MAKE) clobber BUILD_TARGET=therest

