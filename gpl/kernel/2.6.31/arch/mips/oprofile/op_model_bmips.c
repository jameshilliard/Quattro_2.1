/*
 * Copyright (C) 2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/compiler.h>
#include <asm/brcmstb/brcmstb.h>

#include "op_impl.h"

#if defined(OP_DEBUG)
/*	Declare global variable to control debug messages. Initially used as an
 *	enable but may be used as a level of verbosity
 */
#	if defined(OP_DEBUG_INIT_LEVEL)
static int		op_debug_level = OP_DEBUG_INIT_LEVEL;
#	else
static int		op_debug_level = 0;
#	endif
	
/*	Declare print macros for messages, warnings, errors, and debug */
#	define OP_MSG(fmt,...)	printk("OProfile: " fmt,## __VA_ARGS__)
#	define OP_DBG(fmt,...)												\
		if( op_debug_level ) 											\
			printk("%s(%d): " fmt,__FUNCTION__,__LINE__,## __VA_ARGS__)
#else
#	define OP_MSG(fmt,...)
#	define OP_DBG(fmt,...)
#endif

/*! @note The events provided in prior releases are \bnot compatible with 
 * Events coming from opcontrol look like: 0xXYZZ
 *  event[15:12] =   X = ModuleID
 *	event[11:10] =  < not used >
 *  event[ 9: 8] =   Y = SetID
 *	event[ 7]    =  < not used >
 *  event[ 6: 0] =  ZZ = EventID
 *
 * ModuleID and SetID are set globally so they must match for all events.
 * Exception: global events (events with ModuleID 0) can be used regardless
 * of the ModuleID / SetID settings. The upper 
 */

#define BMIPS_MOD(event)		(((event) >> 10) & 0x3c)
#define BMIPS_SET(event)		(((event) >>  8) & 0x03)
#define BMIPS_MOD_SET(event)	BMIPS_MOD(event) | BMIPS_SET(event)
#define BMIPS_EVENT(event)		(((event) >>  0) & 0x7f)

/* Default to using TP0 (HW can only profile one thread at a time) */
/* It is unknown why both TP's are not profiled (other than a decrement by 2).
	Come on, a 32-bit value separate into a 31-bit down-counter and an 
	half-adder tied to a register is not that hard! */
#define PMU_TP					0
#define NUM_COUNTERS			4
/*	Global control enable with the mysterious bit 9 as documented in the Internal
 *	Core Registers document. (...reason unknown)
 */
#if defined(CONFIG_BMIPS3300)
#	define GLOB_ENABLE				0x80000200
#else
#	define GLOB_ENABLE				0x80000000
#endif /* !defined(CONFIG_BMIPS3300) */
#define GLOB_SET_SHIFT			0
#define GLOB_MOD_SHIFT			2
#define CTRL_EVENT_SHIFT		2

#if	defined(USE_ORIGINAL_HANDLER)
/*	Counter MSb - The original handler reads each of the four performance
 *	counter values and checks the MSb of each counter. The counters are
 *	implemented as signed 32-bit counters and it is assumed to have overflowed
 *	when it transitions from 0 (0x00000000) to -1 (0xFFFFFFFF) since each
 *	counter is individually enabled, each control register must also be read to
 *	verify the counter is enabled and the value is negative (less than zero).
 */
#	define CNTR_OVERFLOW			0x80000000
#	define CTRL_ENABLE				0x8001
#else /* else !defined(USE_ORIGINAL_HANDLER) */

/**	@note
 *	Counter overflow enable contains the enable for the counter and the timer
 *	interrupt enable. When the overflow handler option is used, the performance
 *	counter timer interrupt enable (control bit 0) is not set.
 */
#	if defined(USE_OVERFLOW_HANDLING)
#		define CTRL_ENABLE				0x8000
#	else
#		define CTRL_ENABLE				0x8001
#	endif

/** @note
 * Globals status flags provided to allow a single read operation to check the
 * status of all four counters. The interrupt pending is only set when the
 * active counter overflows and the timer interrupt enable (TIE) is set. The
 * overflow is only set if the MSb of the enabled counter.
 *		glob[18] = performance counter 0 enabled and overflow (or MSb)
 *		glob[19] = performance counter 1 enabled and overflow (or MSb)
 *		glob[20] = performance counter 2 enabled and overflow (or MSb)
 *		glob[21] = performance counter 3 enabled and overflow (or MSb)
 *		glob[22] = performance counter 0 enabled and interrupt pending
 *		glob[23] = performance counter 1 enabled and interrupt pending
 *		glob[24] = performance counter 2 enabled and interrupt pending
 *		glob[25] = performance counter 3 enabled and interrupt pending
 * When checking for pending overflow or interrupts, a mask for of all bits is
 * used labeled without an specific counter associated value.
 */
#	define GLOB_INT					0x03c00000
#	define GLOB_INT0				0x00400000
#	define GLOB_INT1				0x00800000
#	define GLOB_INT2				0x01000000
#	define GLOB_INT3				0x02000000
/* Read-only counter overflow bits */
#	define GLOB_OVF					0x003c0000
#	define GLOB_OVF0				0x00040000
#	define GLOB_OVF1				0x00080000
#	define GLOB_OVF2				0x00100000
#	define GLOB_OVF3				0x00200000
#endif /* else !defined(USE_ORIGINAL_HANDLER) */

#if defined(CONFIG_BMIPS3300)
/*	Global Control Register value to enable all counters with the mysterious
 *	bit 9 as documented in the Internal Core Registers document.
 *	(...reason unknown - magic?)
 */
#define GLOB_ENABLE				0x80000200
/* The performance counter registers do not conform to known MIPS standards */
/* Performance Counter read macros */
#define r_perfcntr0()			__read_32bit_c0_register($25, 0)
#define r_perfcntr1()			__read_32bit_c0_register($25, 1)
#define r_perfcntr2()			__read_32bit_c0_register($25, 2)
#define r_perfcntr3()			__read_32bit_c0_register($25, 3)

/* Performance Counter write macros */
#define w_perfcntr0(x)			__write_32bit_c0_register($25, 0, x)
#define w_perfcntr1(x)			__write_32bit_c0_register($25, 1, x)
#define w_perfcntr2(x)			__write_32bit_c0_register($25, 2, x)
#define w_perfcntr3(x)			__write_32bit_c0_register($25, 3, x)

/* Performance Control write macros */
#define w_perfctrl0(x)			__write_32bit_c0_register($25, 4, x)
#define w_perfctrl1(x)			__write_32bit_c0_register($25, 5, x)

/* Performance Globale Control read and write macros */
#define r_glob()				__read_32bit_c0_register($25, 6)
#define w_glob(x)				__write_32bit_c0_register($25, 6, x)

#elif defined(CONFIG_BMIPS4380)
/* Global Control Register value to enable all counters */
#define GLOB_ENABLE				0x80000000

/* The performance counter registers do not conform to known MIPS standards */
/**	@note  Access to the co-processor registers are memory-mapped and is
 *	relative to the co-processor base address.
 */
static unsigned long bmips_cbr;	/*!< co-processor base-address register */

#define PERF_RD(x)				DEV_RD(bmips_cbr + BMIPS_PERF_ ## x)
#define PERF_WR(x, y)			DEV_WR_RB(bmips_cbr + BMIPS_PERF_ ## x, (y))

/* Performance Counter read macros */
#define r_perfcntr0()			PERF_RD(COUNTER_0)
#define r_perfcntr1()			PERF_RD(COUNTER_1)
#define r_perfcntr2()			PERF_RD(COUNTER_2)
#define r_perfcntr3()			PERF_RD(COUNTER_3)

/* Performance Counter write macros */
#define w_perfcntr0(x)			PERF_WR(COUNTER_0, (x))
#define w_perfcntr1(x)			PERF_WR(COUNTER_1, (x))
#define w_perfcntr2(x)			PERF_WR(COUNTER_2, (x))
#define w_perfcntr3(x)			PERF_WR(COUNTER_3, (x))

/* Performance Control write macros */
#define w_perfctrl0(x)			PERF_WR(CONTROL_0, (x))
#define w_perfctrl1(x)			PERF_WR(CONTROL_1, (x))

/* Performance Global Control read and write macros */
#define r_glob()				PERF_RD(GLOBAL_CONTROL)
#define w_glob(x)				PERF_WR(GLOBAL_CONTROL, (x))

#endif

static int (*save_perf_irq)(void);

struct op_mips_model op_model_bmips_ops;

static struct bmips_register_config {
	unsigned int globctrl;
	unsigned int control[4];
	unsigned int counter[4];
} reg;

/* Compute all of the registers in preparation for enabling profiling.  */
static void bmips_reg_setup(struct op_counter_config *counter)
{
	unsigned int	i, event, new_mod_set, mod_set = 0;

	OP_DBG("OProfile register setting up.\n");
	memset(&reg, 0, sizeof(reg));
	reg.globctrl = GLOB_ENABLE;

	for (i = 0; i < NUM_COUNTERS; i++) {
		/* Continue the loop if the counter is disabled */
		if (!counter[i].enabled)
			continue;
		/* Assign the event id and the module and set ID's */
		event		= counter[i].event;
		new_mod_set	= event >> 8;
		/* Compare the module and set ID's if both were non-zero */
		if (mod_set && new_mod_set && mod_set != new_mod_set) {
			printk(KERN_WARNING "%s: profiling event 0x%04x "
				"conflicts with another event, disabling\n",
				__FUNCTION__, event);
			continue;
		}
		/* Assign the reset values for enabling the counters */
		reg.counter[i] = counter[i].count;
		reg.control[i] = CTRL_ENABLE | ((event & 0x7f) << 2);
		/* Assign a counter module and set ID's */
		if (new_mod_set)
			mod_set = new_mod_set;
	}
	/* Assign the reset value for the global control */
	reg.globctrl |= ((mod_set & 0xf0) >> 2) | (mod_set & 3);
	OP_DBG("OProfile register setup.\n");
}

/* Program all of the registers in preparation for enabling profiling.  */
static void bmips_cpu_setup(void *args)
{
	OP_DBG("OProfile CPU setting up.\n");
	w_perfcntr0(reg.counter[0]);
	w_perfcntr1(reg.counter[1]);
	w_perfcntr2(reg.counter[2]);
	w_perfcntr3(reg.counter[3]);
	OP_DBG("OProfile CPU setup.\n");
}

static void bmips_cpu_start(void *args)
{
	OP_DBG("OProfile CPU starting.\n");
	w_perfctrl0(reg.control[0] | (reg.control[1] << 16));
	w_perfctrl1(reg.control[2] | (reg.control[3] << 16));
	w_glob(reg.globctrl);
	OP_DBG("OProfile CPU started.\n");
}

static void bmips_cpu_stop(void *args)
{
	OP_DBG("OProfile CPU stopping.\n");
	w_perfctrl0(0);
	w_perfctrl1(0);
	w_glob(0);
	OP_DBG("OProfile CPU stopped.\n");
}

/*============================================================================*/
/*!	@note This section contains both the original performance counter
 *	interrupt handler and an alternate version that utilizes the interrupt
 *	status bits of the performance counter global control and status register.
 *	(see performance counter definitions above)
 */
#if defined(USE_ORIGINAL_HANDLER)
/*! The original version of this counter handler macro reads the current counter
 *	value, and loads the counter with the stored reset value if the most
 *	significant bit is set,.
 */
#	define HANDLE_COUNTER(n) 							\
		if (r_perfcntr ## n() & CNTR_OVERFLOW) { 		\
			oprofile_add_sample(get_irq_regs(), n); 	\
			w_perfcntr ## n(reg.counter[n]); 			\
			handled = IRQ_HANDLED; 						\
		}

static int bmips_perfcount_handler(void)
{
	int handled = IRQ_NONE;

	if (!(r_glob() & GLOB_ENABLE))
		return handled;

	HANDLE_COUNTER(0)
	HANDLE_COUNTER(1)
	HANDLE_COUNTER(2)
	HANDLE_COUNTER(3)
	/* 	I'm not sure why this is here...
	 *	unless the counters have been highjacked!
	 */
	if (handled == IRQ_HANDLED)
		bmips_cpu_start(NULL);

	return handled;
}

/*----------------------------------------------------------------------------*/
#else
/*! The modified version of this counter handler macro loads the counter with
 *	the stored reset value if the interrupt status is set. The USE_PERF_OVF
 *	definition is provided to allow for configuration of the counters to be
 *	used but only checked when the timer interrupt has been dispatched. This
 *	occurs at a constant interval determined by an external timer.
 */
#	if defined(USE_OVERFLOW_HANDLING)
//#		message	Using Performance Counter overflow handling
/*	"Performance Counter Overflow Handling" */
/*! @note If the USE_PERF_OVF compiler definition is defined, the handler
 *	performs a simply check for a counter overflow condition during the
 *	occurrance of the interrupt, however, no interrupt is actually handled.
 *	The status flags for overflow are used to detect counter overflow condtions.
 */	
#		define HANDLE_NOTHING_PENDING()								\
			if ( (global & (GLOB_ENABLE|GLOB_OVF)) <= GLOB_ENABLE )	\
				return IRQ_NONE;

#		define HANDLE_COUNTER(n)									\
			if ( global & GLOB_OVF ## n ) {							\
				oprofile_add_sample(get_irq_regs(), n);				\
				w_perfcntr ## n(reg.counter[n]);					\
			}

#		define HANDLE_RETURN()		return IRQ_NONE

#	else /* USE_OVERFLOW_HANDLER */
//#		message	Using Performance Counter interrupt handling
/*! @note If the USE_PERF_OVF compiler definition is not defined, the handler
 *	performs interrupt handling by  checking for a counter interrupt condition
 *	during the occurrance of the interrupt and the default value is returned.
 *	The status flags for interrupts are used to detect counter interrupt
 *	 condtions.
 */	
#		define HANDLE_NOTHING_PENDING()								\
	if ( (global & (GLOB_ENABLE|GLOB_INT)) <= GLOB_ENABLE )			\
		return IRQ_NONE;
		
#		define HANDLE_COUNTER(n)									\
			if ( global & GLOB_INT ## n ) {							\
				oprofile_add_sample(get_irq_regs(), n);				\
				w_perfcntr ## n(reg.counter[n]);					\
			}

#		define HANDLE_RETURN()		return IRQ_HANDLED

#	endif /* USE_OVERFLOW_HANDLER */

static int bmips_perfcount_handler(void)
{
#		ifdef OP_DEBUG_INIT_LEVEL
	static int	print_count = 16;
#		endif
	u32		global	= r_glob();
	/*	Check if the performance counters are enabled and if there is any
	 *	pending conditions to be handled. Since enable is 0x80000000 and any
	 *	other bits will make the value larger than enable, only one check is
	 *	needed.
	 */
	HANDLE_NOTHING_PENDING();
#		ifdef OP_DEBUG_INIT_LEVEL
	if( print_count )
		printk("Overflow pending (%08x)...",global);
#		endif
	/* Check each counter using the status flags and handle it if needed */
	HANDLE_COUNTER(0);
	HANDLE_COUNTER(1);
	HANDLE_COUNTER(2);
	HANDLE_COUNTER(3);
	/* 	Return the proper value for either an overflow (IRQ_NONE) or an
	 *	interrupt (IRQ_HANDLED)
	 */
#		ifdef OP_DEBUG_INIT_LEVEL
	if( print_count ) {
		printk("(%08lx).\n",r_glob());
		--print_count;
	}
#		endif
	HANDLE_RETURN();
}

#endif /* !USE_ORIGINAL_HANDLER */
/*----------------------------------------------------------------------------*/

static void bmips_perf_reset(void)
{
	OP_DBG("OProfile reseting.\n");
#if defined(CONFIG_BMIPS4380)
	/**	@note The BMIPS4380 CPU contains only one performance monitoring unit
	 *	(PMU) that operates on only one thread processor (TP) at a time. This
	 *	register is shared between the to 2 TP's and is addressed through a
	 *	fixed offset from the core base register (CBR) memory mapped address.
	 */
	bmips_cbr = BMIPS_GET_CBR();
	change_c0_brcm_cmt_ctrl(0x3 << 30, PMU_TP << 30);
#endif

	w_glob(GLOB_ENABLE);
	w_perfctrl0(0);
	w_perfctrl1(0);
	w_perfcntr0(0);
	w_perfcntr1(0);
	w_perfcntr2(0);
	w_perfcntr3(0);
	w_glob(0);
	OP_DBG("OProfile reset.\n");
}

static int __init bmips_init(void)
{
	OP_DBG("OProfile initializing.\n");
	bmips_perf_reset();

	switch (current_cpu_type()) {
	case CPU_BMIPS3300:
		op_model_bmips_ops.cpu_type = "mips/bmips3300";
		break;
	case CPU_BMIPS4380:
		op_model_bmips_ops.cpu_type = "mips/bmips4380";
		break;
	case CPU_BMIPS5000:
		op_model_bmips_ops.cpu_type = "mips/bmips5000";
		break;
	default:
		BUG();
	}
	save_perf_irq = perf_irq;
	perf_irq = bmips_perfcount_handler;
	OP_DBG("OProfile CPU type is %s.\n",op_model_bmips_ops.cpu_type);
	return 0;
}

static void bmips_exit(void)
{
	OP_DBG("OProfile exiting.\n");
	bmips_perf_reset();
	w_glob(0);
	perf_irq = save_perf_irq;
	OP_DBG("OProfile exited.\n");
}

struct op_mips_model op_model_bmips_ops = {
	.reg_setup	= bmips_reg_setup,
	.cpu_setup	= bmips_cpu_setup,
	.init		= bmips_init,
	.exit		= bmips_exit,
	.cpu_start	= bmips_cpu_start,
	.cpu_stop	= bmips_cpu_stop,
	.num_counters	= 4
};
