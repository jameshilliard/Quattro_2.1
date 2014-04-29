###############################################################################
#
# Copyright (c) 2009 TiVo Inc.
#
###############################################################################

ISM_DEPTH = ./..
include $(ISM_DEPTH)/ismdefs
include $(BOMDEFS)

ifeq ($(BOOTARCH),mips)
    LGENERATED_BOM_FILES = mips.bom 
endif

default: package

include $(BOMRULES)

