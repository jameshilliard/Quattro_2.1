# The default C compiler flags
EXTRA_CFLAGS := -Werror

# If USE_ORIGINAL_HANDLING is defined, the interrupt handler uses the original
# implementation of the interrupt handler. This implementation checks for the
# global enable followed by reading counter and checking the most signifigant
# bit. The return value indicates that the interrupt was handled if any of the
# counters overflowed
#EXTRA_CFLAGS += -DUSE_ORIGINAL_HANDLING

# If USE_OVERFLOW_HANDLING is defined, the interrupt handler checks for counter
# overflow conditions when an interrupt is dispatched. The handler checks for
# any active counter overflow flags and resets the counters, but always the
# return the value always indicates no interrupt was handled.
ifeq  ($(CONFIG_OPROFILE_OVERFLOW_HANDLER),y)
	EXTRA_CFLAGS += -DUSE_OVERFLOW_HANDLING
endif

# If OP_DEBUG is defined, the existing messages, warnings, errors, and debug
# printing and related information is used. The OP_DEBUG_INIT_LEVEL allows the
# initial debug level to be set. Otherwise it is disabled.
ifeq  ($(CONFIG_OPROFILE_DEBUG),y)
	EXTRA_CFLAGS += -DOP_DEBUG -DOP_DEBUG_INIT_LEVEL=1
endif

# Object files required for the OPto
obj-$(CONFIG_OPROFILE) += oprofile.o

DRIVER_OBJS = $(addprefix ../../../drivers/oprofile/, \
		oprof.o cpu_buffer.o buffer_sync.o \
		event_buffer.o oprofile_files.o \
		oprofilefs.o oprofile_stats.o \
		timer_int.o )

oprofile-y := $(DRIVER_OBJS) common.o

oprofile-$(CONFIG_CPU_MIPS32)		+= op_model_mipsxx.o
oprofile-$(CONFIG_CPU_MIPS64)		+= op_model_mipsxx.o
oprofile-$(CONFIG_CPU_R10000)		+= op_model_mipsxx.o
oprofile-$(CONFIG_CPU_SB1)			+= op_model_mipsxx.o
oprofile-$(CONFIG_CPU_RM9000)		+= op_model_rm9000.o
oprofile-$(CONFIG_BMIPS3300)		+= op_model_bmips.o
oprofile-$(CONFIG_BMIPS4380)		+= op_model_bmips.o
oprofile-$(CONFIG_BMIPS5000)		+= op_model_bmips.o
