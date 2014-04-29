#
# File: Makefile
#
# Copyright (C) 1998-1999 TiVo, Inc.  All rights reserved.
#
###############################################################################

ISM_DEPTH       := ./..
include $(ISM_DEPTH)/ismdefs

DIRS := common

ifeq ($(BOOTARCH),x86)
DIRS += server
else
DIRS += client
endif

include $(ISMRULES)
