/*
 * Copyright (C) 2009 Broadcom Corporation
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/smp.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/mii.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <asm/cpu-info.h>
#include <asm/r4kcache.h>
#include <asm/mipsregs.h>
#include <asm/cacheflush.h>
#include <asm/brcmstb/brcmstb.h>

#if 0
#define DBG			printk
#else
#define DBG(...)		do { } while(0)
#endif

/***********************************************************************
 * CPU divisor / PLL manipulation
 ***********************************************************************/

#ifdef CONFIG_BMIPS5000
/* CP0 COUNT/COMPARE frequency is affected by the PLL input but not divisor */
#define FIXED_COUNTER_FREQ	1
#else
/* CP0 COUNT/COMPARE frequency is affected by the PLL input AND the divisor */
#define FIXED_COUNTER_FREQ	0
#endif

/* MIPS active standby on 7550 */
#define CPU_PLL_MODE1		216000

/*
 * current ADJUSTED base frequency (reflects the current PLL settings)
 * brcm_cpu_khz (in time.c) always has the ORIGINAL clock frequency and
 *   is never changed after bootup
 */
unsigned long brcm_adj_cpu_khz = 0;

/* current CPU divisor, as set by the user */
static int cpu_div = 1;

/*
 * MIPS clockevent code always assumes the original boot-time CP0 clock rate.
 * This function scales the number of ticks according to the current HW
 * settings.
 */
unsigned long brcm_fixup_ticks(unsigned long delta)
{
	unsigned long long tmp = delta;

	if (unlikely(!brcm_adj_cpu_khz))
		brcm_adj_cpu_khz = brcm_cpu_khz;
#if FIXED_COUNTER_FREQ
	tmp = (tmp * brcm_adj_cpu_khz) / brcm_cpu_khz;
#else
	tmp = (tmp * brcm_adj_cpu_khz) / (brcm_cpu_khz * cpu_div);
#endif
	return (unsigned long)tmp;
}

static unsigned int orig_udelay_val[NR_CPUS];

struct spd_change {
	int			old_div;
	int			new_div;
	int			old_base;
	int			new_base;
};

void brcm_set_cpu_speed(void *arg)
{
	struct spd_change *c = arg;
	uint32_t new_div = (uint32_t)c->new_div;
	unsigned long __maybe_unused count, compare, delta;
	int cpu = smp_processor_id();
	uint32_t __maybe_unused tmp0, tmp1, tmp2, tmp3;

	/* scale udelay_val */
	if (!orig_udelay_val[cpu])
		orig_udelay_val[cpu] = current_cpu_data.udelay_val;
	if (c->new_base == brcm_cpu_khz)
		current_cpu_data.udelay_val = orig_udelay_val[cpu] / new_div;
	else
		current_cpu_data.udelay_val =
			(unsigned long long)orig_udelay_val[cpu] *
			c->new_base / (new_div * c->old_base);

	/* scale any pending timer events */
	compare = read_c0_compare();
	count = read_c0_count();

#if !FIXED_COUNTER_FREQ
	if (compare > count)
		delta = ((unsigned long long)(compare - count) *
			c->old_div * c->new_base) /
			(new_div * c->old_base);
	else
		delta = ((unsigned long long)(count - compare) *
			c->old_div * c->new_base) /
			(new_div * c->old_base);
#else
	if (compare > count)
		delta = ((unsigned long long)(compare - count) *
			c->new_base) / c->old_base;
	else
		delta = ((unsigned long long)(count - compare) *
			c->new_base) / c->old_base;
#endif
	write_c0_compare(read_c0_count() + delta);

	if (cpu != 0)
		return;

#if defined(CONFIG_BRCM_CPU_PLL)
	brcm_adj_cpu_khz = c->new_base;
#if defined(CONFIG_BCM7550)
	if (brcm_adj_cpu_khz == CPU_PLL_MODE1) {
		/* 216Mhz */
		BDEV_WR_RB(BCHP_VCXO_CTL_CONFIG_FSM_PLL_NEXT_CFG_3A,
			0x801b2806);
		BDEV_WR_RB(BCHP_VCXO_CTL_CONFIG_FSM_PLL_NEXT_CFG_3B,
			0x00300618);
	} else {
		/* 324Mhz */
		BDEV_WR_RB(BCHP_VCXO_CTL_CONFIG_FSM_PLL_NEXT_CFG_3A,
			0x801b2806);
		BDEV_WR_RB(BCHP_VCXO_CTL_CONFIG_FSM_PLL_NEXT_CFG_3B,
			0x00300418);
	}
	BDEV_WR_RB(BCHP_VCXO_CTL_CONFIG_FSM_PLL_UPDATE, 1);
#else
#error CPU PLL adjustment not supported on this chip
#endif
#endif

	new_div = ffs(new_div) - 1;

	/* see BMIPS datasheet, CP0 register $22 */

#if defined(CONFIG_BMIPS3300)
	change_c0_brcm_bus_pll(0x07 << 22, (new_div << 23) | (0 << 22));
#elif defined(CONFIG_BMIPS5000)
	change_c0_brcm_mode(0x0f << 4, (1 << 7) | (new_div << 4));
#elif defined(CONFIG_BMIPS4380)
	__asm__ __volatile__(
	"	.set	push			\n"
	"	.set	noreorder		\n"
	"	.set	nomacro			\n"
	"	.set	mips32			\n"
	/* get kseg1 address for CBA into %3 */
	"	mfc0	%3, $22, 6		\n"
	"	li	%2, 0xfffc0000		\n"
	"	and	%3, %2			\n"
	"	li	%2, 0xa0000000		\n"
	"	add	%3, %2			\n"
	/* %1 = async bit, %2 = mask out everything but 30:28 */
	"	lui	%1, 0x1000		\n"
	"	lui	%2, 0x8fff		\n"
	"	beqz	%0, 1f			\n"
	"	ori	%2, 0xffff		\n"
	/* handle SYNC to ASYNC */
	"	sync				\n"
	"	mfc0	%4, $22, 5		\n"
	"	and	%4, %2			\n"
	"	or	%4, %1			\n"
	"	mtc0	%4, $22, 5		\n"
	"	nop				\n"
	"	nop				\n"
	"	lw	%2, 4(%3)		\n"
	"	sw	%2, 4(%3)		\n"
	"	sync				\n"
	"	sll	%0, 29			\n"
	"	or	%4, %0			\n"
	"	mtc0	%4, $22, 5		\n"
	"	nop; nop; nop; nop		\n"
	"	nop; nop; nop; nop		\n"
	"	nop; nop; nop; nop		\n"
	"	nop; nop; nop; nop		\n"
	"	b	2f			\n"
	"	nop				\n"
	/* handle ASYNC to SYNC */
	"1:					\n"
	"	mfc0	%4, $22, 5		\n"
	"	and	%4, %2			\n"
	"	or	%4, %1			\n"
	"	mtc0	%4, $22, 5		\n"
	"	nop; nop; nop; nop		\n"
	"	nop; nop; nop; nop		\n"
	"	sync				\n"
	"	and	%4, %2			\n"
	"	mtc0	%4, $22, 5		\n"
	"	nop				\n"
	"	nop				\n"
	"	lw	%2, 4(%3)		\n"
	"	sw	%2, 4(%3)		\n"
	"	sync				\n"
	"2:					\n"
	"	.set	pop			\n"
	: "+r" (new_div),
	  "=&r" (tmp0), "=&r" (tmp1), "=&r" (tmp2), "=&r" (tmp3));
#endif
}

#ifdef CONFIG_BRCM_CPU_DIV

ssize_t brcm_pm_show_cpu_div(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", cpu_div);
}

ssize_t brcm_pm_store_cpu_div(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	struct spd_change chg;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val != 1 && val != 2 && val != 4 && val != 8
#if defined(CONFIG_BMIPS5000)
		&& val != 16
#endif
			)
		return -EINVAL;

	chg.old_div = cpu_div;
	chg.new_div = val;
	chg.old_base = brcm_adj_cpu_khz;
	chg.new_base = brcm_adj_cpu_khz;

	on_each_cpu(brcm_set_cpu_speed, &chg, 1);
	cpu_div = val;
	return count;
}

#endif /* CONFIG_BRCM_CPU_DIV */

#ifdef CONFIG_BRCM_CPU_PLL

static int cpu_pll_mode = 0;

ssize_t brcm_pm_show_cpu_pll(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", cpu_pll_mode);
}

ssize_t brcm_pm_store_cpu_pll(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	struct spd_change chg;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (cpu_pll_mode == val)
		return count;

	switch (val) {
	case 0:
		chg.new_base = brcm_cpu_khz;
		break;
	case 1:
		chg.new_base = CPU_PLL_MODE1;
		break;
	default:
		return -EINVAL;
	}

	chg.old_div = cpu_div;
	chg.new_div = cpu_div;
	chg.old_base = brcm_adj_cpu_khz;
	on_each_cpu(brcm_set_cpu_speed, &chg, 1);

	cpu_pll_mode = val;
	return count;
}

#endif /* CONFIG_BRCM_CPU_PLL */

/***********************************************************************
 * USB / ENET / GENET / MoCA / SATA PM common internal functions
 ***********************************************************************/

struct clk {
	char			name[16];
	spinlock_t		lock;
	int			refcnt;
	int			(*cb)(int, void *);
	void			*cb_arg;
	void			(*disable)(void);
	void			(*enable)(void);
};

static void brcm_pm_sata_disable(void);
static void brcm_pm_sata_enable(void);
static void brcm_pm_enet_disable(void);
static void brcm_pm_enet_enable(void);
static void brcm_pm_moca_disable(void);
static void brcm_pm_moca_enable(void);
static void brcm_pm_usb_disable(void);
static void brcm_pm_usb_enable(void);
static void brcm_pm_set_ddr_timeout(int);

static int brcm_pm_ddr_timeout;
static unsigned long brcm_pm_standby_flags;

static struct clk brcm_clk_table[] = {
	{
		.name		= "sata",
		.lock		= SPIN_LOCK_UNLOCKED,
		.refcnt		= 0,
		.disable	= &brcm_pm_sata_disable,
		.enable		= &brcm_pm_sata_enable,
	},
	{
		.name		= "enet",
		.lock		= SPIN_LOCK_UNLOCKED,
		.refcnt		= 0,
		.disable	= &brcm_pm_enet_disable,
		.enable		= &brcm_pm_enet_enable,
	},
	{
		.name		= "moca",
		.lock		= SPIN_LOCK_UNLOCKED,
		.refcnt		= 0,
		.disable	= &brcm_pm_moca_disable,
		.enable		= &brcm_pm_moca_enable,
	},
	{
		.name		= "usb",
		.lock		= SPIN_LOCK_UNLOCKED,
		.refcnt		= 0,
		.disable	= &brcm_pm_usb_disable,
		.enable		= &brcm_pm_usb_enable,
	},
};

struct clk *brcm_pm_clk_find(const char *name)
{
	int i;
	struct clk *clk = brcm_clk_table;
	for (i = 0; i < ARRAY_SIZE(brcm_clk_table); i++, clk++)
		if (!strcmp(name, clk->name))
			return clk;
	return NULL;
}

/* sysfs attributes */

ssize_t brcm_pm_show_usb_power(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct clk *clk = brcm_pm_clk_find("usb");
	return snprintf(buf, PAGE_SIZE, "%d\n", !!clk->refcnt);
}

ssize_t brcm_pm_store_usb_power(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct clk *clk = brcm_pm_clk_find("usb");
	int val;

	if (!clk || !clk->cb || sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	return clk->cb(val ? PM_EVENT_RESUME : PM_EVENT_SUSPEND,
		clk->cb_arg) ? : count;
}

ssize_t brcm_pm_show_sata_power(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct clk *clk = brcm_pm_clk_find("sata");
	return snprintf(buf, PAGE_SIZE, "%d\n", !!clk->refcnt);
}

ssize_t brcm_pm_store_sata_power(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct clk *clk = brcm_pm_clk_find("sata");
	int val;

	if (!clk || !clk->cb || sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	return clk->cb(val ? PM_EVENT_RESUME : PM_EVENT_SUSPEND,
		clk->cb_arg) ? : count;
}

ssize_t brcm_pm_show_ddr_timeout(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", brcm_pm_ddr_timeout);
}

ssize_t brcm_pm_store_ddr_timeout(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	brcm_pm_ddr_timeout = val;
	if (brcm_pm_enabled)
		brcm_pm_set_ddr_timeout(val);
	return count;
}

ssize_t brcm_pm_show_standby_flags(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%lx\n", brcm_pm_standby_flags);
}

ssize_t brcm_pm_store_standby_flags(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val;
	if (sscanf(buf, "%lx", &val) != 1)
		return -EINVAL;

	brcm_pm_standby_flags = val;
	return count;
}

/* Boot time functions */

static int nopm_setup(char *str)
{
	brcm_pm_enabled = 0;
	return 0;
}

__setup("nopm", nopm_setup);

/***********************************************************************
 * USB / ENET / GENET / MoCA / SATA PM external API
 ***********************************************************************/

struct clk *clk_get(struct device *dev, const char *id)
{
	return brcm_pm_clk_find(id) ? : ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	if (clk && !IS_ERR(clk)) {
		spin_lock_irqsave(&clk->lock, flags);
		if (++(clk->refcnt) == 1 && brcm_pm_enabled) {
			printk(KERN_DEBUG "brcm-pm: enabling %s clocks\n",
				clk->name);
			clk->enable();
		}
		spin_unlock_irqrestore(&clk->lock, flags);
		return 0;
	} else {
		return -EINVAL;
	}
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;
	if (clk && !IS_ERR(clk)) {
		spin_lock_irqsave(&clk->lock, flags);
		if (--(clk->refcnt) == 0 && brcm_pm_enabled) {
			printk(KERN_DEBUG "brcm-pm: disabling %s clocks\n",
				clk->name);
			clk->disable();
		}
		spin_unlock_irqrestore(&clk->lock, flags);
	}
}
EXPORT_SYMBOL(clk_disable);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int brcm_pm_register_cb(char *name, int (*fn)(int, void *), void *arg)
{
	struct clk *clk = brcm_pm_clk_find(name);
	unsigned long flags;

	if (!clk)
		return -ENOENT;

	spin_lock_irqsave(&clk->lock, flags);
	BUG_ON(fn && clk->cb);
	clk->cb = fn;
	clk->cb_arg = arg;
	spin_unlock_irqrestore(&clk->lock, flags);

	return 0;
}
EXPORT_SYMBOL(brcm_pm_register_cb);

int brcm_pm_unregister_cb(char *name)
{
	return brcm_pm_register_cb(name, NULL, NULL);
}
EXPORT_SYMBOL(brcm_pm_unregister_cb);

/***********************************************************************
 * USB / ENET / GENET / MoCA / SATA PM implementations (per-chip)
 ***********************************************************************/

#define PLL_DIS(x)		BDEV_WR_RB(BCHP_##x, 0x04)
#define PLL_ENA(x)		do { \
					BDEV_WR_RB(BCHP_##x, 0x03); \
					mdelay(1); \
				} while(0)

static void brcm_pm_sata_disable(void)
{
#if defined(CONFIG_BCM7125)
	BDEV_WR_F_RB(SUN_TOP_CTRL_GENERAL_CTRL_1, sata_ana_pwrdn, 1);
	BDEV_WR_F_RB(CLKGEN_SATA_CLK_PM_CTRL, DIS_CLK_99P7, 1);
	BDEV_WR_F_RB(CLKGEN_SATA_CLK_PM_CTRL, DIS_CLK_216, 1);
	BDEV_WR_F_RB(CLKGEN_SATA_CLK_PM_CTRL, DIS_CLK_108, 1);
	PLL_DIS(VCXO_CTL_MISC_RAP_AVD_PLL_CHL_4);
#elif defined(CONFIG_BCM7420)
	BDEV_WR_F_RB(SUN_TOP_CTRL_GENERAL_CTRL_1, sata_ana_pwrdn, 1);
	BDEV_WR_F_RB(CLK_SATA_CLK_PM_CTRL, DIS_SATA_PCI_CLK, 1);
	BDEV_WR_F_RB(CLK_SATA_CLK_PM_CTRL, DIS_108M_CLK, 1);
	BDEV_WR_F_RB(CLK_SATA_CLK_PM_CTRL, DIS_216M_CLK, 1);
	PLL_DIS(CLK_GENET_NETWORK_PLL_4);
#endif
}

static void brcm_pm_sata_enable(void)
{
#if defined(CONFIG_BCM7125)
	PLL_ENA(VCXO_CTL_MISC_RAP_AVD_PLL_CHL_4);
	BDEV_WR_F_RB(CLKGEN_SATA_CLK_PM_CTRL, DIS_CLK_108, 0);
	BDEV_WR_F_RB(CLKGEN_SATA_CLK_PM_CTRL, DIS_CLK_216, 0);
	BDEV_WR_F_RB(CLKGEN_SATA_CLK_PM_CTRL, DIS_CLK_99P7, 0);
	BDEV_WR_F_RB(SUN_TOP_CTRL_GENERAL_CTRL_1, sata_ana_pwrdn, 0);
#elif defined(CONFIG_BCM7420)
	PLL_ENA(CLK_GENET_NETWORK_PLL_4);
	BDEV_WR_F_RB(CLK_SATA_CLK_PM_CTRL, DIS_216M_CLK, 0);
	BDEV_WR_F_RB(CLK_SATA_CLK_PM_CTRL, DIS_108M_CLK, 0);
	BDEV_WR_F_RB(CLK_SATA_CLK_PM_CTRL, DIS_SATA_PCI_CLK, 0);
	BDEV_WR_F_RB(SUN_TOP_CTRL_GENERAL_CTRL_1, sata_ana_pwrdn, 0);
#endif
}

static void brcm_pm_enet_disable(void)
{
}

static void brcm_pm_enet_enable(void)
{
}

static void brcm_pm_moca_disable(void)
{
}

static void brcm_pm_moca_enable(void)
{
}

static void brcm_pm_usb_disable(void)
{
#if defined(CONFIG_BCM7125)
	BDEV_WR_F_RB(USB_CTRL_UTMI_CTL_1, UTMI_IDDQ, 1);
	BDEV_WR_F_RB(USB_CTRL_UTMI_CTL_1, UTMI_SOFT_RESETB, 0x00);
	BDEV_WR_F_RB(USB_CTRL_UTMI_CTL_1, PHY_PWDNB, 0x00);
	BDEV_WR_F_RB(USB_CTRL_PLL_CTL_1, PLL_PWRDWNB, 0);
	BDEV_WR_F_RB(USB_CTRL_PLL_CTL_1, XTAL_PWRDWNB, 0);
	BDEV_WR_F_RB(CLKGEN_USB_CLK_PM_CTRL, DIS_CLK_216, 1);
	BDEV_WR_F_RB(CLKGEN_USB_CLK_PM_CTRL, DIS_CLK_108, 1);
	PLL_DIS(CLKGEN_PLL_MAIN_CH4_PM_CTRL);
#endif
}

static void brcm_pm_usb_enable(void)
{
#if defined(CONFIG_BCM7125)
	PLL_ENA(CLKGEN_PLL_MAIN_CH4_PM_CTRL);
	BDEV_WR_F_RB(CLKGEN_USB_CLK_PM_CTRL, DIS_CLK_108, 0);
	BDEV_WR_F_RB(CLKGEN_USB_CLK_PM_CTRL, DIS_CLK_216, 0);
	BDEV_WR_F_RB(USB_CTRL_PLL_CTL_1, XTAL_PWRDWNB, 1);
	BDEV_WR_F_RB(USB_CTRL_PLL_CTL_1, PLL_PWRDWNB, 1);
	BDEV_WR_F_RB(USB_CTRL_UTMI_CTL_1, PHY_PWDNB, 0x0f);
	BDEV_WR_F_RB(USB_CTRL_UTMI_CTL_1, UTMI_SOFT_RESETB, 0x0f);
	BDEV_WR_F_RB(USB_CTRL_UTMI_CTL_1, UTMI_IDDQ, 0);
#endif
}

static void brcm_pm_set_ddr_timeout(int val)
{
#if defined(CONFIG_BCM7125) || defined(CONFIG_BCM7420) || \
		defined(CONFIG_BCM7340) || defined(CONFIG_BCM7342)
	if (val) {
		BDEV_WR_F(MEMC_DDR23_APHY_AC_0_DDR_PAD_CNTRL,
			IDDQ_MODE_ON_SELFREF, 1);
		BDEV_WR_F(MEMC_DDR23_APHY_AC_0_DDR_PAD_CNTRL,
			HIZ_ON_SELFREF, 1);
		BDEV_WR_F(MEMC_DDR23_APHY_AC_0_DDR_PAD_CNTRL,
			DEVCLK_OFF_ON_SELFREF, 1);
		BDEV_WR_F(MEMC_DDR23_APHY_AC_0_POWERDOWN,
			PLLCLKS_OFF_ON_SELFREF, 1);
		BDEV_WR_F(MEMC_DDR23_APHY_WL0_0_DDR_PAD_CNTRL,
			IDDQ_MODE_ON_SELFREF, 1);
		BDEV_WR_F(MEMC_DDR23_APHY_WL1_0_DDR_PAD_CNTRL,
			IDDQ_MODE_ON_SELFREF, 1);

		BDEV_WR_F_RB(MEMC_DDR_0_SRPD_CONFIG, INACT_COUNT, 0xdff);
		BDEV_WR_F(MEMC_DDR_0_SRPD_CONFIG, SRPD_EN, 1);
	} else {
		unsigned long flags;

		local_irq_save(flags);
		BDEV_WR_F(MEMC_DDR_0_SRPD_CONFIG, INACT_COUNT, 0xffff);
		do {
			DEV_RD(KSEG1);
		} while (BDEV_RD_F(MEMC_DDR_0_POWER_DOWN_STATUS, SRPD));
		BDEV_WR_F(MEMC_DDR_0_SRPD_CONFIG, SRPD_EN, 0);
		local_irq_restore(flags);
	}
#endif
}

/***********************************************************************
 * Passive standby
 ***********************************************************************/

#ifdef CONFIG_BRCM_HAS_STANDBY

static suspend_state_t suspend_state;

static int brcm_pm_prepare(void)
{
	DBG("%s:%d\n", __FUNCTION__, __LINE__);
	return 0;
}

static int brcm_pm_standby(void)
{
	u32 entry, vec = ebase;
	u32 oldvec[4];

	DBG("%s:%d\n", __FUNCTION__, __LINE__);

	brcm_irq_standby_enter(BRCM_IRQ_PM);

	/* install jump to special PM exception handler */
	if((read_c0_config() & (1 << 31)) &&
	   (read_c0_config1() & (1 << 31)) &&
	   (read_c0_config2() & (1 << 31)) &&
	   (read_c0_config3() & (1 << 5))) {
		/* vectored interrupts (MIPS 24k/34k VInt bit is set) */

		/* IntCtl 9:5 = vector spacing */
		vec += 0x200 + 2 * (read_c0_intctl() & 0x3e0);
	} else {
		/* interrupts use the general exception vector on this CPU */
		vec += 0x200;
	}

	oldvec[0] = DEV_RD(vec + 0);
	oldvec[1] = DEV_RD(vec + 4);
	oldvec[2] = DEV_RD(vec + 8);
	oldvec[3] = DEV_RD(vec + 12);

	entry = (u32)&brcm_pm_irq;
	DEV_WR(vec + 0, 0x3c1a0000 | (entry >> 16));	  // lui k0, HI(entry)
	DEV_WR(vec + 4, 0x375a0000 | (entry & 0xffff));	  // ori k0, LO(entry)
	DEV_WR(vec + 8, 0x03400008);			  // jr k0
	DEV_WR(vec + 12, 0x00000000);			  // nop

	flush_icache_range(vec, vec + 16);

	brcm_pm_standby_asm(current_cpu_data.icache.linesz, ebase);

	DEV_WR(vec + 0, oldvec[0]);
	DEV_WR(vec + 4, oldvec[1]);
	DEV_WR(vec + 8, oldvec[2]);
	DEV_WR(vec + 12, oldvec[3]);
	flush_icache_range(vec, vec + 16);

	brcm_irq_standby_exit();
	return 0;
}

static int brcm_pm_enter(suspend_state_t unused)
{
	int ret = 0;

	DBG("%s:%d\n", __FUNCTION__, __LINE__);
	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	//case PM_SUSPEND_MEM:
		ret = brcm_pm_standby();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void brcm_pm_finish(void)
{
	DBG("%s:%d\n", __FUNCTION__, __LINE__);
}

/* Hooks to enable / disable UART interrupts during suspend */
static int brcm_pm_begin(suspend_state_t state)
{
	DBG("%s:%d\n", __FUNCTION__, __LINE__);
	suspend_state = state;
	return 0;
}

static void brcm_pm_end(void)
{
	DBG("%s:%d\n", __FUNCTION__, __LINE__);
	suspend_state = PM_SUSPEND_ON;
	return;
}

static int brcm_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_STANDBY;
}

static struct platform_suspend_ops brcm_pm_ops = {
	.begin		= brcm_pm_begin,
	.end		= brcm_pm_end,
	.prepare	= brcm_pm_prepare,
	.enter		= brcm_pm_enter,
	.finish		= brcm_pm_finish,
	.valid		= brcm_pm_valid,
};

static int brcm_suspend_init(void)
{
	DBG("%s:%d\n", __FUNCTION__, __LINE__);
	suspend_set_ops(&brcm_pm_ops);
	return 0;
}
late_initcall(brcm_suspend_init);

#endif /* CONFIG_BRCM_HAS_STANDBY */
