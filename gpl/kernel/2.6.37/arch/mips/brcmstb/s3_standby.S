/*
 * Copyright (C) 2011 Broadcom Corporation
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

#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/cacheops.h>
#include <asm/addrspace.h>
#include <asm/brcmstb/brcmstb.h>

#define REG_OFFSET		(KSEG1 | BCHP_PHYSICAL_OFFSET)
#define REG(x)			(REG_OFFSET + (x))


	.macro dbg_save_val	src
	li	t1, REG(BCHP_UARTA_SCR);
	sw	src, 0(t1)
	.endm

	.macro aon_write_data index reg
	li	t1, REG(AON_RAM_BASE)
	sw	\reg, \index(t1)
	.endm

	.macro aon_read_data index reg
	li	t1, REG(AON_RAM_BASE)
	lw	\reg, \index(t1)
	.endm

/*	AON data map
	offset		bits
	0:		0:	0 - cold boot, 1 - warm boot
	4:		0-31:	reentry address
 */
	.text
	.set		noreorder
	.align		5

#if defined(CONFIG_BMIPS5000)
#define CACHE_WRITEBACK_CMD	Hit_Writeback_Inv_SD
#else
#define CACHE_WRITEBACK_CMD	Hit_Writeback_Inv_D
#endif

/*
 * We do not need stack frame, the entire context gets saved
 * in a dedicated structure brcm_pm_s3_context
 * Upon warm reboot the context gets restored from this structure
 * Parameters:
 *	a0: options
 *	a1: dram encoder start function or NULL if not installed
 */
	LEAF(brcm_pm_s3_standby_asm)

	la	t0, s3_context
	/* general purpose registers */
	sw	ra, 0(t0)
	sw	s0, 4(t0)
	sw	s1, 8(t0)
	sw	s2, 12(t0)
	sw	s3, 16(t0)
	sw	s4, 20(t0)
	sw	s5, 24(t0)
	sw	s6, 28(t0)
	sw	s7, 32(t0)
	sw	gp, 36(t0)
	sw	sp, 40(t0)
	sw	fp, 44(t0)

	mfc0	t1, CP0_STATUS
# save cp0 sr
	sw	t1, 48(t0)

	/* Write-back gp registers */
	cache	CACHE_WRITEBACK_CMD, 0(t0)
	nop
	cache	CACHE_WRITEBACK_CMD, 32(t0)
	nop
	cache	CACHE_WRITEBACK_CMD, 64(t0)
	nop

	/*
	 * Call DRAM encoder start method if it was installed
	 * Nobody can modify DRAM content by now.
	 */
	beqz	a1, skip_dram_encode
	nop

	jal	a1
	nop

skip_dram_encode:

	li	t0, REG(BCHP_AON_CTRL_PM_MIPS_WAIT_COUNT)
	li	t1, 0xffff
	sw	t1, 0(t0)
	lw	zero, 0(t0)

# set 'warm boot' flag
	li	t0, 1
	aon_write_data	0, t0
# reentry point
	la	t0, s3_reentry
	aon_write_data  4, t0

# deep power down request

pd_request:
	li	t0, REG(BCHP_AON_CTRL_PM_CTRL)
# SET: pm_deep_standby, pm_warm_boot, pm_enable_pll_pwrdn
	li	t1, 0x32
	sw	zero, 0(t0)
	lw	zero, 0(t0)
	sw	t1, 0(t0)
	lw	zero, 0(t0)

# SET: pm_start_pwrdn
	li	t1, 0x33
	sw	t1, 0(t0)
	lw	zero, 0(t0)

/***********************************************************************
 * Wait for interrupt
 ***********************************************************************/
wait_loop:
# enable int2 and then wait for an interrupt
	mfc0	t0, CP0_STATUS

	li	t1, ~(ST0_IM | ST0_IE)
	and	t0, t1
	ori	t0, STATUSF_IP2
	mtc0	t0, CP0_STATUS
	nop
	nop
	nop
	ori	t0, ST0_IE
	mtc0	t0, CP0_STATUS
	wait
	nop

s3_reentry:
# restore sr
	sync
	nop

s3_standby_exit:
#if defined(CONFIG_BMIPS4380)
	mfc0	t0, $22, 3
	li	t1, 0x1ff0
	li	t2, (1 << 12) | (1 << 9)
	or	t0, t1
	xor	t0, t1
	or	t0, t2
	mtc0	t0, $22, 3
#elif defined(CONFIG_BMIPS5000)
	mfc0	t0, $22, 5
	li	t1, 0x01ff
	li	t2, (1 << 8) | (1 << 5)
	or	t0, t1
	xor	t0, t1
	or	t0, t2
	mtc0	t0, $22, 5
#endif
	sync
	nop

#ifdef CONFIG_BRCM_UPPER_MEMORY
	li	sp, BRCM_WARM_RESTART_VEC
	la	k0, brcm_upper_tlb_setup
	jalr	k0
	nop
#endif

# return to caller
	/* general purpose registers */
	la	t0, s3_context
	lw	ra, 0(t0)
	lw	s0, 4(t0)
	lw	s1, 8(t0)
	lw	s2, 12(t0)
	lw	s3, 16(t0)
	lw	s4, 20(t0)
	lw	s5, 24(t0)
	lw	s6, 28(t0)
	lw	s7, 32(t0)
	lw	gp, 36(t0)
	lw	sp, 40(t0)
	lw	fp, 44(t0)

# restore cp0 sr
	lw	t1, 48(t0)
	mtc0	t1, CP0_STATUS

	li	v0, 1
	jr	ra
	nop

	END(brcm_pm_s3_standby_asm)

#define UART_LSR_OFFSET		0x14
#define UART_LSR_MASK		BCHP_UARTA_LSR_THRE_MASK
#define UART_TX_OFFSET		0x00

__dputc:

	li	t1, REG(BCHP_UARTA_REG_START)
1:
	lw	t2, UART_LSR_OFFSET(t1)
	andi	t2, UART_LSR_MASK
	beqz	t2, 1b
	nop

	sw	a0, UART_TX_OFFSET(t1)

1:	lw	t2, UART_LSR_OFFSET(t1)
	andi	t2, UART_LSR_MASK
	beqz	t2, 1b
	nop
	jr	ra
	nop

s3_standby_section_end:
	nop
