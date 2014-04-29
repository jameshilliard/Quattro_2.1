#
# Makefile for the Broadcom NAND MTD
#

obj-$(CONFIG_MTD_BRCMNAND)	+= brcmnand.o

brcmnand-objs-$(CONFIG_MTD_BRCMNAND_EDU) += edu.o brcmnand_isr.o
brcmnand-objs-$(CONFIG_MTD_BRCMNAND_CORRECTABLE_ERR_HANDLING) += brcmnand_cet.o

ifeq ($(CONFIG_TIVO_YUNDO),y)
brcmnand-objs			+= bcm7xxx-nand-samsung.o
brcmnand-objs			+= brcmnand_base-samsung.o
brcmnand-objs			+= brcmnand_bbt-samsung.o $(brcmnand-objs-y)
else
brcmnand-objs			+= bcm7xxx-nand.o brcmnand_base.o \
				   brcmnand_bbt.o $(brcmnand-objs-y)
endif

#EXTRA_CFLAGS			+= -Werror
