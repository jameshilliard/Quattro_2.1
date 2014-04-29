#
# Makefile for TiVo common code 
# Copyright (C) 2009 TiVo Inc
# 
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y	 += crashallassert.o gen-ioctl.o sgat-dio.o setup.o monotime.o cmdline.o

ifdef CONFIG_TIVO_PLOG
obj-y	 += plog.o
endif

ifdef CONFIG_TIVO_WATCHDOG       
obj-y    += tivo-hwwatchdog.o
endif

ifdef CONFIG_TIVO_CONSDBG
obj-y    += debug.o
endif

ifdef CONFIG_TIVO_PANICLOG
obj-y    += paniclog.o
endif
