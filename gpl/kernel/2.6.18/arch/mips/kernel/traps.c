/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 01, 06 Ralf Baechle
 * Copyright (C) 1995, 1996 Paul M. Antoine
 * Copyright (C) 1998 Ulf Carlsson
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 01 MIPS Technologies, Inc.
 * Copyright (C) 2002, 2003, 2004, 2005  Maciej W. Rozycki
 *
 * KGDB specific changes - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/bootmem.h>
#include <linux/kgdb.h>
#include <linux/interrupt.h>

#include <asm/atomic.h>
#include <asm/bootinfo.h>
#include <asm/branch.h>
#include <asm/break.h>
#include <asm/cpu.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/module.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/tlbdebug.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/watch.h>
#include <asm/types.h>
#include <asm/kdebug.h>
#include <asm/unistd.h>

#ifdef CONFIG_TIVO_PLOG
#include <linux/plog.h>
#include <linux/tivo-reset.h>
#endif
extern asmlinkage void handle_int(void);
extern asmlinkage void handle_tlbm(void);
extern asmlinkage void handle_tlbl(void);
extern asmlinkage void handle_tlbs(void);
extern asmlinkage void handle_adel(void);
extern asmlinkage void handle_ades(void);
extern asmlinkage void handle_ibe(void);
extern asmlinkage void handle_dbe(void);
extern asmlinkage void handle_sys(void);
extern asmlinkage void handle_bp(void);
extern asmlinkage void handle_ri(void);
extern asmlinkage void handle_cpu(void);
extern asmlinkage void handle_ov(void);
extern asmlinkage void handle_tr(void);
extern asmlinkage void handle_fpe(void);
extern asmlinkage void handle_mdmx(void);
extern asmlinkage void handle_watch(void);
extern asmlinkage void handle_mt(void);
extern asmlinkage void handle_dsp(void);
extern asmlinkage void handle_mcheck(void);
extern asmlinkage void handle_reserved(void);

extern int fpu_emulator_cop1Handler(struct pt_regs *xcp,
	struct mips_fpu_struct *ctx, int has_fpu);

extern void dump_module_addrs(void);

void (*board_be_init)(void);
int (*board_be_handler)(struct pt_regs *regs, int is_fixup);
void (*board_nmi_handler_setup)(void);
void (*board_ejtag_handler_setup)(void);
void (*board_bind_eic_interrupt)(int irq, int regset);

atomic_t rdhwr_ctr = ATOMIC_INIT(0), brdhwr_ctr = ATOMIC_INIT(0);

static struct pt_regs * _trace_kern(unsigned int *pc, unsigned int *sp,
                                    unsigned int *fp, unsigned int *ra,
				    int dispconsole,struct task_struct *tsk);
static void _trace_user(struct pt_regs * regs, int dispconsole, 
                        struct task_struct *tsk);

static void show_raw_backtrace(unsigned long reg29)
{
	unsigned long *sp = (unsigned long *)reg29;
	unsigned long addr;

	printk("Call Trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif
	while (!kstack_end(sp)) {
		addr = *sp++;
		if (__kernel_text_address(addr))
			print_ip_sym(addr);
	}
	printk("\n");
}

#ifdef CONFIG_KALLSYMS
static int raw_show_trace;
static int __init set_raw_show_trace(char *str)
{
	raw_show_trace = 1;
	return 1;
}
__setup("raw_show_trace", set_raw_show_trace);

extern unsigned long unwind_stack(struct task_struct *task, unsigned long *sp,
				  unsigned long pc, unsigned long *ra);

static void show_backtrace(struct task_struct *task, struct pt_regs *regs)
{
	unsigned long sp = regs->regs[29];
	unsigned long ra = regs->regs[31];
	unsigned long pc = regs->cp0_epc;

	if (raw_show_trace || !__kernel_text_address(pc)) {
		show_raw_backtrace(sp);
		return;
	}
	printk("Call Trace:\n");
	do {
		print_ip_sym(pc);
		pc = unwind_stack(task, &sp, pc, &ra);
	} while (pc);
	printk("\n");
}
#else
#define show_backtrace(task, r) show_raw_backtrace((r)->regs[29]);
#endif

ATOMIC_NOTIFIER_HEAD(mips_die_chain);

int register_die_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&mips_die_chain, nb);
}

/*
 * This routine abuses get_user()/put_user() to reference pointers
 * with at least a bit of error checking ...
 */
static void show_stacktrace(struct task_struct *task, struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	long stackdata;
	int i;
	unsigned long *sp = (unsigned long *)regs->regs[29];

	printk("Stack :");
	i = 0;
	while ((unsigned long) sp & (PAGE_SIZE - 1)) {
		if (i && ((i % (64 / field)) == 0))
			printk("\n       ");
		if (i > 256) {
			printk(" ...");
			break;
		}

		if (__get_user(stackdata, sp++)) {
			printk(" (Bad stack address)");
			break;
		}

		printk(" %0*lx", field, stackdata);
		i++;
	}
	printk("\n");
	show_backtrace(task, regs);
}

static __always_inline void prepare_frametrace(struct pt_regs *regs)
{
	__asm__ __volatile__(
		".set push\n\t"
		".set noat\n\t"
#ifdef CONFIG_64BIT
		"1: dla $1, 1b\n\t"
		"sd $1, %0\n\t"
		"sd $29, %1\n\t"
		"sd $31, %2\n\t"
#else
		"1: la $1, 1b\n\t"
		"sw $1, %0\n\t"
		"sw $29, %1\n\t"
		"sw $31, %2\n\t"
#endif
		".set pop\n\t"
		: "=m" (regs->cp0_epc),
		"=m" (regs->regs[29]), "=m" (regs->regs[31])
		: : "memory");
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	struct pt_regs regs;
	if (sp) {
		regs.regs[29] = (unsigned long)sp;
		regs.regs[31] = 0;
		regs.cp0_epc = 0;
	} else {
		if (task && task != current) {
			regs.regs[29] = task->thread.reg29;
			regs.regs[31] = 0;
			regs.cp0_epc = task->thread.reg31;
		} else {
			prepare_frametrace(&regs);
		}
	}
	show_stacktrace(task, &regs);
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	struct pt_regs regs;

	/*
	 * Remove any garbage that may be in regs (specially func
	 * addresses) to avoid show_raw_backtrace() to report them
	 */
	memset(&regs, 0, sizeof(regs));
	prepare_frametrace(&regs);
	show_backtrace(current, &regs);
}

EXPORT_SYMBOL(dump_stack);

void show_code(unsigned int *pc)
{
	long i;

	printk("\nCode:");

	for(i = -3 ; i < 6 ; i++) {
		unsigned int insn;
		if (__get_user(insn, pc + i)) {
			printk(" (Bad address in epc)\n");
			break;
		}
		printk("%c%08x%c", (i?' ':'<'), insn, (i?' ':'>'));
	}
}

void show_regs(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int cause = regs->cp0_cause;
	int i;

	printk("Cpu %d\n", smp_processor_id());

	/*
	 * Saved main processor registers
	 */
	for (i = 0; i < 32; ) {
		if ((i % 4) == 0)
			printk("$%2d   :", i);
		if (i == 0)
			printk(" %0*lx", field, 0UL);
		else if (i == 26 || i == 27)
			printk(" %*s", field, "");
		else
			printk(" %0*lx", field, regs->regs[i]);

		i++;
		if ((i % 4) == 0)
			printk("\n");
	}

	printk("Hi    : %0*lx\n", field, regs->hi);
	printk("Lo    : %0*lx\n", field, regs->lo);

	/*
	 * Saved cp0 registers
	 */
	printk("epc   : %0*lx ", field, regs->cp0_epc);
	print_symbol("%s ", regs->cp0_epc);
	printk("    %s\n", print_tainted());
	printk("ra    : %0*lx ", field, regs->regs[31]);
	print_symbol("%s\n", regs->regs[31]);

	printk("Status: %08x    ", (uint32_t) regs->cp0_status);

	if (current_cpu_data.isa_level == MIPS_CPU_ISA_I) {
		if (regs->cp0_status & ST0_KUO)
			printk("KUo ");
		if (regs->cp0_status & ST0_IEO)
			printk("IEo ");
		if (regs->cp0_status & ST0_KUP)
			printk("KUp ");
		if (regs->cp0_status & ST0_IEP)
			printk("IEp ");
		if (regs->cp0_status & ST0_KUC)
			printk("KUc ");
		if (regs->cp0_status & ST0_IEC)
			printk("IEc ");
	} else {
		if (regs->cp0_status & ST0_KX)
			printk("KX ");
		if (regs->cp0_status & ST0_SX)
			printk("SX ");
		if (regs->cp0_status & ST0_UX)
			printk("UX ");
		switch (regs->cp0_status & ST0_KSU) {
		case KSU_USER:
			printk("USER ");
			break;
		case KSU_SUPERVISOR:
			printk("SUPERVISOR ");
			break;
		case KSU_KERNEL:
			printk("KERNEL ");
			break;
		default:
			printk("BAD_MODE ");
			break;
		}
		if (regs->cp0_status & ST0_ERL)
			printk("ERL ");
		if (regs->cp0_status & ST0_EXL)
			printk("EXL ");
		if (regs->cp0_status & ST0_IE)
			printk("IE ");
	}
	printk("\n");

	printk("Cause : %08x\n", cause);

	cause = (cause & CAUSEF_EXCCODE) >> CAUSEB_EXCCODE;
	if (1 <= cause && cause <= 5)
		printk("BadVA : %0*lx\n", field, regs->cp0_badvaddr);

	printk("PrId  : %08x\n", read_c0_prid());
}

void show_registers(struct pt_regs *regs)
{
	show_regs(regs);
	print_modules();
	printk("Process %s (pid: %d, threadinfo=%p, task=%p)\n",
	        current->comm, current->pid, current_thread_info(), current);
	show_stacktrace(current, regs);
	show_code((unsigned int *) regs->cp0_epc);
	printk("\n");
}

#ifdef CONFIG_TIVO_IRQ_DEBUG
extern void show_interrupts_on_die();

/* dummy_die() is used only to manipulate the stack pointer
 * so as to prevent the forward  stack values from being overwritten
 * by die() itself.
 * Using it only in case of either page fault or the reservered 
 * instruction kind of panic.
 */
NORET_TYPE void ATTRIB_NORET dummy_die (const char * str, struct pt_regs * regs)
{
	asm ("addiu   $29,$29,-1024;");/**** 256 words ****/
	die (str, regs);
}
#endif

static DEFINE_SPINLOCK(die_lock);

#ifdef CONFIG_TIVO
extern void show_last_irqs(void);
#endif

NORET_TYPE void ATTRIB_NORET die(const char * str, struct pt_regs * regs)
{
	static int die_counter;
#ifdef CONFIG_MIPS_MT_SMTC
	unsigned long dvpret = dvpe();
#endif /* CONFIG_MIPS_MT_SMTC */

#ifdef CONFIG_TIVO_PLOG
	/* Disabling plog: sooner the better */
	_plog_enable(0);
#endif

/* This is a temporary debug code for a specific issue, we have been experiencing */
#ifdef CONFIG_TIVO_IRQ_DEBUG
	unsigned long *sp, *sp_temp;
	unsigned long i, addr, sp_cur;
	unsigned long stack_page = task_stack_page(current);
	
	sp_cur = regs->regs[29]; 
	sp = (unsigned long *)regs->regs[29];
	sp -= 256; /* going to move maximum upto 256 words down the stack */

	if ((unsigned long)sp < stack_page) /* don't go beyond the stack page */
		sp = (unsigned long *)stack_page;

    if (!__kernel_text_address(regs->cp0_epc)) { /* applicable only when epc is bogus */
		i = 0;
		sp_temp = sp;
		printk("FRONT Stack :");
		while ((unsigned long)sp < sp_cur) {
			addr = *sp++;
			
			if (i && ((i % 8) == 0))
				printk("\n       ");
			
			printk(" %08x", addr);
			i++;
		}

		sp = sp_temp;
		printk("\nFORWARD Call Trace:\n");
		while ((unsigned long)sp < sp_cur) {
			addr = *sp++;

			if (__kernel_text_address(addr)) 
				print_ip_sym(addr);
		}
	}
#endif

	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
#ifdef CONFIG_MIPS_MT_SMTC
	mips_mt_regdump(dvpret);
#endif /* CONFIG_MIPS_MT_SMTC */
	printk("%s[#%d]:\n", str, ++die_counter);
	show_registers(regs);
#ifdef CONFIG_TIVO_IRQ_DEBUG
	show_interrupts_on_die();
#endif
#ifdef CONFIG_TIVO
	show_last_irqs();
#endif
	spin_unlock_irq(&die_lock);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops) {
		printk(KERN_EMERG "Fatal exception: panic in 5 seconds\n");
		ssleep(5);
		panic("Fatal exception");
	}

#ifdef CONFIG_TIVO
        panic("Die called\n");
#endif

	do_exit(SIGSEGV);
}

extern const struct exception_table_entry __start___dbe_table[];
extern const struct exception_table_entry __stop___dbe_table[];

void __declare_dbe_table(void)
{
	__asm__ __volatile__(
	".section\t__dbe_table,\"a\"\n\t"
	".previous"
	);
}

/* Given an address, look for it in the exception tables. */
static const struct exception_table_entry *search_dbe_tables(unsigned long addr)
{
	const struct exception_table_entry *e;

	e = search_extable(__start___dbe_table, __stop___dbe_table - 1, addr);
	if (!e)
		e = search_module_dbetables(addr);
	return e;
}

asmlinkage void do_be(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	const struct exception_table_entry *fixup = NULL;
	int data = regs->cp0_cause & 4;
	int action = MIPS_BE_FATAL;

	/* XXX For now.  Fixme, this searches the wrong table ...  */
	if (data && !user_mode(regs))
		fixup = search_dbe_tables(exception_epc(regs));

	if (fixup)
		action = MIPS_BE_FIXUP;

	if (board_be_handler)
		action = board_be_handler(regs, fixup != 0);

	switch (action) {
	case MIPS_BE_DISCARD:
		return;
	case MIPS_BE_FIXUP:
		if (fixup) {
			regs->cp0_epc = fixup->nextinsn;
			return;
		}
		break;
	default:
		break;
	}

	/*
	 * Assume it would be too dangerous to continue ...
	 */
	printk(KERN_ALERT "%s bus error, epc == %0*lx, ra == %0*lx\n",
	       data ? "Data" : "Instruction",
	       field, regs->cp0_epc, field, regs->regs[31]);
#ifdef CONFIG_TIVO
        printk("Got Bus Error at %08x\n", (unsigned)regs->cp0_epc);
        die_if_kernel("Bus error induced panic", regs);
#else
	die_if_kernel("Oops", regs);
#endif
	force_sig(SIGBUS, current);
}

static inline int get_insn_opcode(struct pt_regs *regs, unsigned int *opcode)
{
	unsigned int __user *epc;

	epc = (unsigned int __user *) regs->cp0_epc +
	      ((regs->cp0_cause & CAUSEF_BD) != 0);
	if (!get_user(*opcode, epc))
		return 0;

	force_sig(SIGSEGV, current);
	return 1;
}

/*
 * ll/sc emulation
 */

#define OPCODE 0xfc000000
#define BASE   0x03e00000
#define RT     0x001f0000
#define OFFSET 0x0000ffff
#define LL     0xc0000000
#define SC     0xe0000000
#define SPEC3  0x7c000000
#define BRDHWR 0xec000000
#define RD     0x0000f800
#define FUNC   0x0000003f
#define RDHWR  0x0000003b

/*
 * mfc0 emulation
 */

#define MFC0   0x40000000

/*
 * The ll_bit is cleared by r*_switch.S
 */

unsigned long ll_bit;

static struct task_struct *ll_task = NULL;

static inline void simulate_ll(struct pt_regs *regs, unsigned int opcode)
{
	unsigned long value, __user *vaddr;
	long offset;
	int signal = 0;

	/*
	 * analyse the ll instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */

	/* sign extend offset */
	offset = opcode & OFFSET;
	offset <<= 16;
	offset >>= 16;

	vaddr = (unsigned long __user *)
	        ((unsigned long)(regs->regs[(opcode & BASE) >> 21]) + offset);

	if ((unsigned long)vaddr & 3) {
		signal = SIGBUS;
		goto sig;
	}
	if (get_user(value, vaddr)) {
		signal = SIGSEGV;
		goto sig;
	}

	preempt_disable();

	if (ll_task == NULL || ll_task == current) {
		ll_bit = 1;
	} else {
		ll_bit = 0;
	}
	ll_task = current;

	preempt_enable();

	compute_return_epc(regs);

	regs->regs[(opcode & RT) >> 16] = value;

	return;

sig:
	force_sig(signal, current);
}

static inline void simulate_sc(struct pt_regs *regs, unsigned int opcode)
{
	unsigned long __user *vaddr;
	unsigned long reg;
	long offset;
	int signal = 0;

	/*
	 * analyse the sc instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */

	/* sign extend offset */
	offset = opcode & OFFSET;
	offset <<= 16;
	offset >>= 16;

	vaddr = (unsigned long __user *)
	        ((unsigned long)(regs->regs[(opcode & BASE) >> 21]) + offset);
	reg = (opcode & RT) >> 16;

	if ((unsigned long)vaddr & 3) {
		signal = SIGBUS;
		goto sig;
	}

	preempt_disable();

	if (ll_bit == 0 || ll_task != current) {
		compute_return_epc(regs);
		regs->regs[reg] = 0;
		preempt_enable();
		return;
	}

	preempt_enable();

	if (put_user(regs->regs[reg], vaddr)) {
		signal = SIGSEGV;
		goto sig;
	}

	compute_return_epc(regs);
	regs->regs[reg] = 1;

	return;

sig:
	force_sig(signal, current);
}

/*
 * ll uses the opcode of lwc0 and sc uses the opcode of swc0.  That is both
 * opcodes are supposed to result in coprocessor unusable exceptions if
 * executed on ll/sc-less processors.  That's the theory.  In practice a
 * few processors such as NEC's VR4100 throw reserved instruction exceptions
 * instead, so we're doing the emulation thing in both exception handlers.
 */
static inline int simulate_llsc(struct pt_regs *regs)
{
	unsigned int opcode;

	if (unlikely(get_insn_opcode(regs, &opcode)))
		return -EFAULT;

	if ((opcode & OPCODE) == LL) {
		simulate_ll(regs, opcode);
		return 0;
	}
	if ((opcode & OPCODE) == SC) {
		simulate_sc(regs, opcode);
		return 0;
	}

	return -EFAULT;			/* Strange things going on ... */
}

/*
 * Simulate trapping 'rdhwr' instructions to provide user accessible
 * registers not implemented in hardware.  The only current use of this
 * is the thread area pointer.
 */
static inline int simulate_rdhwr(struct pt_regs *regs)
{
	struct thread_info *ti = task_thread_info(current);
	unsigned int opcode;
	int rd, rt;

	if (unlikely(get_insn_opcode(regs, &opcode)))
		return -EFAULT;

	if (unlikely(compute_return_epc(regs)))
		return -EFAULT;
	
	rd = (opcode & RD) >> 11;
	rt = (opcode & RT) >> 16;

#if defined(CONFIG_MIPS_BRCM97XXX) && defined(_brcm_rdhwr)
	/* PR34054: use alternate RDHWR instruction encoding */
	if (((opcode & OPCODE) == BRDHWR && (opcode & FUNC) == RDHWR)
	    || ((opcode & OPCODE) == SPEC3 && (opcode & FUNC) == RDHWR)) {
#else
	if ((opcode & OPCODE) == SPEC3 && (opcode & FUNC) == RDHWR) {
#endif
		int rd = (opcode & RD) >> 11;
		int rt = (opcode & RT) >> 16;

		if((opcode & OPCODE) == BRDHWR)
			atomic_inc(&brdhwr_ctr);
		else
			atomic_inc(&rdhwr_ctr);

		switch (rd) {
			case 29:
				regs->regs[rt] = ti->tp_value;
				return 0;
			default:
				return -EFAULT;
		}
	}

	/*
	 * allow some CP0 registers to be read from user programs
	 */

#define CP0_OFF(reg, idx) (((reg) << 11) | (idx))
#define EMU_MFC0(reg, idx) \
	case CP0_OFF(reg, idx): \
		regs->regs[rt] = __read_32bit_c0_register($ ## reg, idx); \
		return(0)

	if ((opcode & OPCODE) == MFC0) {
		switch(opcode & OFFSET)
		{
			EMU_MFC0(15, 0);

			EMU_MFC0(16, 0);
			EMU_MFC0(16, 1);
			EMU_MFC0(16, 7);

			EMU_MFC0(22, 0);
			EMU_MFC0(22, 1);
			EMU_MFC0(22, 2);
			EMU_MFC0(22, 3);
			EMU_MFC0(22, 4);
			EMU_MFC0(22, 5);
			EMU_MFC0(22, 6);
		}
	}

	/* Not ours.  */
	return -EFAULT;
}

asmlinkage void do_ov(struct pt_regs *regs)
{
	siginfo_t info;

	die_if_kernel("Integer overflow", regs);

	info.si_code = FPE_INTOVF;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_addr = (void __user *) regs->cp0_epc;
	force_sig_info(SIGFPE, &info, current);
}

/*
 * XXX Delayed fp exceptions when doing a lazy ctx switch XXX
 */
asmlinkage void do_fpe(struct pt_regs *regs, unsigned long fcr31)
{
	die_if_kernel("FP exception in kernel code", regs);

	MARK(kernel_trap_entry, "%d struct pt_regs %p",
		CAUSE_EXCCODE(regs->cp0_cause), regs);
	if (fcr31 & FPU_CSR_UNI_X) {
		int sig;

		/*
		 * Unimplemented operation exception.  If we've got the full
		 * software emulator on-board, let's use it...
		 *
		 * Force FPU to dump state into task/thread context.  We're
		 * moving a lot of data here for what is probably a single
		 * instruction, but the alternative is to pre-decode the FP
		 * register operands before invoking the emulator, which seems
		 * a bit extreme for what should be an infrequent event.
		 */
		preempt_disable();

		/* We might have lost fpu before disabling preempt... */
		if (is_fpu_owner())
			save_fp(current);
		/* Ensure 'resume' not overwrite saved fp context again. */
		lose_fpu();

		preempt_enable();

		/* Run the emulator */
		sig = fpu_emulator_cop1Handler (regs, &current->thread.fpu, 1);

		/*
		 * We can't allow the emulated instruction to leave any of
		 * the cause bit set in $fcr31.
		 */
		current->thread.fpu.fcr31 &= ~FPU_CSR_ALL_X;

		preempt_disable();

		own_fpu();	/* Using the FPU again.  */
		/* Restore the hardware register state */
		restore_fp(current);

		preempt_enable();

		/* If something went wrong, signal */
		if (sig)
			force_sig(sig, current);

		return;
	}

	force_sig(SIGFPE, current);
}

asmlinkage void do_bp(struct pt_regs *regs)
{
	unsigned int opcode, bcode;
	siginfo_t info;

	die_if_kernel("Break instruction in kernel code", regs);

	if (get_insn_opcode(regs, &opcode))
		return;

	/*
	 * There is the ancient bug in the MIPS assemblers that the break
	 * code starts left to bit 16 instead to bit 6 in the opcode.
	 * Gas is bug-compatible, but not always, grrr...
	 * We handle both cases with a simple heuristics.  --macro
	 */
	bcode = ((opcode >> 6) & ((1 << 20) - 1));
	if (bcode < (1 << 10))
		bcode <<= 10;

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all break
	 * insns, even for break codes that indicate arithmetic failures.
	 * Weird ...)
	 * But should we continue the brokenness???  --macro
	 */
	switch (bcode) {
	case BRK_OVERFLOW << 10:
	case BRK_DIVZERO << 10:
		if (bcode == (BRK_DIVZERO << 10))
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void __user *) regs->cp0_epc;
		force_sig_info(SIGFPE, &info, current);
		break;
	default:
		force_sig(SIGTRAP, current);
	}
}

asmlinkage void do_tr(struct pt_regs *regs)
{
	unsigned int opcode, tcode = 0;
	siginfo_t info;

	die_if_kernel("Trap instruction in kernel code", regs);

	if (get_insn_opcode(regs, &opcode))
		return;

	/* Immediate versions don't provide a code.  */
	if (!(opcode & OPCODE))
		tcode = ((opcode >> 6) & ((1 << 10) - 1));

#ifdef CONFIG_TIVO
        /* Special "TiVo opcode" for querying the cpu counter */
        if (opcode == 0x50f4) { 
                extern void tivo_time_getcount(struct pt_regs *regs);

                if (!compute_return_epc(regs))
                        tivo_time_getcount(regs);
                return;
        }
#endif
	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all trap
	 * insns, even for trap codes that indicate arithmetic failures.
	 * Weird ...)
	 * But should we continue the brokenness???  --macro
	 */
	switch (tcode) {
	case BRK_OVERFLOW:
	case BRK_DIVZERO:
		if (tcode == BRK_DIVZERO)
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void __user *) regs->cp0_epc;
		force_sig_info(SIGFPE, &info, current);
		break;
	default:
		force_sig(SIGTRAP, current);
	}
}

asmlinkage void do_ri(struct pt_regs *regs)
{
#ifdef CONFIG_TIVO_IRQ_DEBUG
	if (unlikely(!user_mode(regs)))
		dummy_die("Reserved instruction in kernel code", regs);
#else
	die_if_kernel("Reserved instruction in kernel code", regs);
#endif

	if (!cpu_has_llsc)
		if (!simulate_llsc(regs))
			return;

	if (!simulate_rdhwr(regs))
		return;

	force_sig(SIGILL, current);
}

asmlinkage void do_cpu(struct pt_regs *regs)
{
	unsigned int cpid;

    MARK(kernel_trap_entry, "%d struct pt_regs %p",
		CAUSE_EXCCODE(regs->cp0_cause), regs);
    
	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;

	switch (cpid) {
	case 0:
		die_if_kernel("do_cpu invoked from kernel context!", regs);
		if (!cpu_has_llsc)
			if (!simulate_llsc(regs)) {
				MARK(kernel_trap_exit, MARK_NOARGS);
				return;
			}

        if (!simulate_rdhwr(regs)) {
			MARK(kernel_trap_exit, MARK_NOARGS);
			return;
		}

		break;

	case 1:
		if (!test_thread_flag(TIF_ALLOW_FP_IN_KERNEL))
			die_if_kernel("do_cpu invoked from kernel context!",
				      regs);
		preempt_disable();

		own_fpu();
		if (used_math()) {	/* Using the FPU again.  */
			restore_fp(current);
		} else {			/* First time FPU user.  */
			init_fpu();
			set_used_math();
		}

		if (cpu_has_fpu) {
			preempt_enable();
			if (test_thread_flag(TIF_ALLOW_FP_IN_KERNEL)) {
				local_irq_disable();
				if (cpu_has_fpu)
					regs->cp0_status |= ST0_CU1;
				/*
				 * We must return without enabling
				 * interrupts to ensure keep FPU
				 * ownership until resume.
				 */
				return;
			}
		} else {
			int sig;
			preempt_enable();
			sig = fpu_emulator_cop1Handler(regs,
						&current->thread.fpu, 0);
			if (sig)
				force_sig(sig, current);
#ifdef CONFIG_MIPS_MT_FPAFF
			else {
			/*
			 * MIPS MT processors may have fewer FPU contexts
			 * than CPU threads. If we've emulated more than
			 * some threshold number of instructions, force
			 * migration to a "CPU" that has FP support.
			 */
			 if(mt_fpemul_threshold > 0
			 && ((current->thread.emulated_fp++
			    > mt_fpemul_threshold))) {
			  /*
			   * If there's no FPU present, or if the
			   * application has already restricted
			   * the allowed set to exclude any CPUs
			   * with FPUs, we'll skip the procedure.
			   */
			  if (cpus_intersects(current->cpus_allowed,
			  			mt_fpu_cpumask)) {
			    cpumask_t tmask;

			    cpus_and(tmask,
					current->thread.user_cpus_allowed,
					mt_fpu_cpumask);
			    set_cpus_allowed(current, tmask);
			    current->thread.mflags |= MF_FPUBOUND;
			  }
			 }
			}
#endif /* CONFIG_MIPS_MT_FPAFF */
		}
		MARK(kernel_trap_exit, MARK_NOARGS);
		return;

	case 2:
	case 3:
		die_if_kernel("do_cpu invoked from kernel context!", regs);
		break;
	}

	force_sig(SIGILL, current);
	MARK(kernel_trap_exit, MARK_NOARGS);
}

asmlinkage void do_mdmx(struct pt_regs *regs)
{
	force_sig(SIGILL, current);
}

asmlinkage void do_watch(struct pt_regs *regs)
{
	/*
	 * We use the watch exception where available to detect stack
	 * overflows.
	 */
	dump_tlb_all();
	show_regs(regs);
	panic("Caught WATCH exception - probably caused by stack overflow.");
}

asmlinkage void do_mcheck(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	int multi_match = regs->cp0_status & ST0_TS;

	show_regs(regs);

	if (multi_match) {
		printk("Index   : %0x\n", read_c0_index());
		printk("Pagemask: %0x\n", read_c0_pagemask());
		printk("EntryHi : %0*lx\n", field, read_c0_entryhi());
		printk("EntryLo0: %0*lx\n", field, read_c0_entrylo0());
		printk("EntryLo1: %0*lx\n", field, read_c0_entrylo1());
		printk("\n");
		dump_tlb_all();
	}

	show_code((unsigned int *) regs->cp0_epc);

	/*
	 * Some chips may have other causes of machine check (e.g. SB1
	 * graduation timer)
	 */
	panic("Caught Machine Check exception - %scaused by multiple "
	      "matching entries in the TLB.",
	      (multi_match) ? "" : "not ");
}

asmlinkage void do_mt(struct pt_regs *regs)
{
	int subcode;

	subcode = (read_vpe_c0_vpecontrol() & VPECONTROL_EXCPT)
			>> VPECONTROL_EXCPT_SHIFT;
	switch (subcode) {
	case 0:
		printk(KERN_DEBUG "Thread Underflow\n");
		break;
	case 1:
		printk(KERN_DEBUG "Thread Overflow\n");
		break;
	case 2:
		printk(KERN_DEBUG "Invalid YIELD Qualifier\n");
		break;
	case 3:
		printk(KERN_DEBUG "Gating Storage Exception\n");
		break;
	case 4:
		printk(KERN_DEBUG "YIELD Scheduler Exception\n");
		break;
	case 5:
		printk(KERN_DEBUG "Gating Storage Schedulier Exception\n");
		break;
	default:
		printk(KERN_DEBUG "*** UNKNOWN THREAD EXCEPTION %d ***\n",
			subcode);
		break;
	}
	die_if_kernel("MIPS MT Thread exception in kernel", regs);

	force_sig(SIGILL, current);
}


asmlinkage void do_dsp(struct pt_regs *regs)
{
	if (cpu_has_dsp)
		panic("Unexpected DSP exception\n");

	force_sig(SIGILL, current);
}

asmlinkage void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.  Most probably
	 * caused by a new unknown cpu type or after another deadly
	 * hard/software error.
	 */
	show_regs(regs);
	panic("Caught reserved exception %ld - should not happen.",
	      (regs->cp0_cause & 0x7f) >> 2);
}

asmlinkage void do_default_vi(struct pt_regs *regs)
{
	show_regs(regs);
	panic("Caught unexpected vectored interrupt.");
}

/*
 * Some MIPS CPUs can enable/disable for cache parity detection, but do
 * it different ways.
 */
static inline void parity_protection_init(void)
{
	switch (current_cpu_data.cputype) {
	case CPU_24K:
	case CPU_34K:
	case CPU_5KC:
		write_c0_ecc(0x80000000);
		back_to_back_c0_hazard();
		/* Set the PE bit (bit 31) in the c0_errctl register. */
		printk(KERN_INFO "Cache parity protection %sabled\n",
		       (read_c0_ecc() & 0x80000000) ? "en" : "dis");
		break;
	case CPU_20KC:
	case CPU_25KF:
		/* Clear the DE bit (bit 16) in the c0_status register. */
		printk(KERN_INFO "Enable cache parity protection for "
		       "MIPS 20KC/25KF CPUs.\n");
		clear_c0_status(ST0_DE);
		break;
	default:
		break;
	}
}

asmlinkage void cache_parity_error(void)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int reg_val;

	/* For the moment, report the problem and hang. */
	printk("Cache error exception:\n");
	printk("cp0_errorepc == %0*lx\n", field, read_c0_errorepc());
	reg_val = read_c0_cacheerr();
	printk("c0_cacheerr == %08x\n", reg_val);

	printk("Decoded c0_cacheerr: %s cache fault in %s reference.\n",
	       reg_val & (1<<30) ? "secondary" : "primary",
	       reg_val & (1<<31) ? "data" : "insn");
	printk("Error bits: %s%s%s%s%s%s%s\n",
	       reg_val & (1<<29) ? "ED " : "",
	       reg_val & (1<<28) ? "ET " : "",
	       reg_val & (1<<26) ? "EE " : "",
	       reg_val & (1<<25) ? "EB " : "",
	       reg_val & (1<<24) ? "EI " : "",
	       reg_val & (1<<23) ? "E1 " : "",
	       reg_val & (1<<22) ? "E0 " : "");
	printk("IDX: 0x%08x\n", reg_val & ((1<<22)-1));

#if defined(CONFIG_CPU_MIPS32) || defined(CONFIG_CPU_MIPS64)
	if (reg_val & (1<<22))
		printk("DErrAddr0: 0x%0*lx\n", field, read_c0_derraddr0());

	if (reg_val & (1<<23))
		printk("DErrAddr1: 0x%0*lx\n", field, read_c0_derraddr1());
#endif

	panic("Can't handle the cache error!");
}

/*
 * SDBBP EJTAG debug exception handler.
 * We skip the instruction and return to the next instruction.
 */
void ejtag_exception_handler(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned long depc, old_epc;
	unsigned int debug;

	printk(KERN_DEBUG "SDBBP EJTAG debug exception - not handled yet, just ignored!\n");
	depc = read_c0_depc();
	debug = read_c0_debug();
	printk(KERN_DEBUG "c0_depc = %0*lx, DEBUG = %08x\n", field, depc, debug);
	if (debug & 0x80000000) {
		/*
		 * In branch delay slot.
		 * We cheat a little bit here and use EPC to calculate the
		 * debug return address (DEPC). EPC is restored after the
		 * calculation.
		 */
		old_epc = regs->cp0_epc;
		regs->cp0_epc = depc;
		__compute_return_epc(regs);
		depc = regs->cp0_epc;
		regs->cp0_epc = old_epc;
	} else
		depc += 4;
	write_c0_depc(depc);

#if 0
	printk(KERN_DEBUG "\n\n----- Enable EJTAG single stepping ----\n\n");
	write_c0_debug(debug | 0x100);
#endif
}

/*
 * NMI exception handler.
 */
void nmi_exception_handler(struct pt_regs *regs)
{
#ifdef CONFIG_MIPS_MT_SMTC
	unsigned long dvpret = dvpe();
	bust_spinlocks(1);
	printk("NMI taken!!!!\n");
	mips_mt_regdump(dvpret);
#else
	bust_spinlocks(1);
	printk("NMI taken!!!!\n");
#endif /* CONFIG_MIPS_MT_SMTC */
	die("NMI", regs);
	while(1) ;
}

#define VECTORSPACING 0x100	/* for EI/VI mode */

#ifdef CONFIG_DISCONTIGMEM
/* this is not set up yet because we called tlb_init() early */
unsigned long ebase = CAC_BASE;
#else
unsigned long ebase;
#endif
unsigned long exception_handlers[32];
unsigned long vi_handlers[64];

/*
 * As a side effect of the way this is implemented we're limited
 * to interrupt handlers in the address range from
 * KSEG0 <= x < KSEG0 + 256mb on the Nevada.  Oh well ...
 */
void *set_except_vector(int n, void *addr)
{
	unsigned long handler = (unsigned long) addr;
	unsigned long old_handler = exception_handlers[n];

	exception_handlers[n] = handler;
	if (n == 0 && cpu_has_divec) {
		*(volatile u32 *)(ebase + 0x200) = 0x08000000 |
		                                 (0x03ffffff & (handler >> 2));
		flush_icache_range(ebase + 0x200, ebase + 0x204);
	}
	return (void *)old_handler;
}

#ifdef CONFIG_CPU_MIPSR2_SRS
/*
 * MIPSR2 shadow register set allocation
 * FIXME: SMP...
 */

static struct shadow_registers {
	/*
	 * Number of shadow register sets supported
	 */
	unsigned long sr_supported;
	/*
	 * Bitmap of allocated shadow registers
	 */
	unsigned long sr_allocated;
} shadow_registers;

static void mips_srs_init(void)
{
	shadow_registers.sr_supported = ((read_c0_srsctl() >> 26) & 0x0f) + 1;
	printk(KERN_INFO "%d MIPSR2 register sets available\n",
	       shadow_registers.sr_supported);
	shadow_registers.sr_allocated = 1;	/* Set 0 used by kernel */
}

int mips_srs_max(void)
{
	return shadow_registers.sr_supported;
}

int mips_srs_alloc(void)
{
	struct shadow_registers *sr = &shadow_registers;
	int set;

again:
	set = find_first_zero_bit(&sr->sr_allocated, sr->sr_supported);
	if (set >= sr->sr_supported)
		return -1;

	if (test_and_set_bit(set, &sr->sr_allocated))
		goto again;

	return set;
}

void mips_srs_free(int set)
{
	struct shadow_registers *sr = &shadow_registers;

	clear_bit(set, &sr->sr_allocated);
}

static void *set_vi_srs_handler(int n, void *addr, int srs)
{
	unsigned long handler;
	unsigned long old_handler = vi_handlers[n];
	u32 *w;
	unsigned char *b;

	if (!cpu_has_veic && !cpu_has_vint)
		BUG();

	if (addr == NULL) {
		handler = (unsigned long) do_default_vi;
		srs = 0;
	} else
		handler = (unsigned long) addr;
	vi_handlers[n] = (unsigned long) addr;

	b = (unsigned char *)(ebase + 0x200 + n*VECTORSPACING);

	if (srs >= mips_srs_max())
		panic("Shadow register set %d not supported", srs);

	if (cpu_has_veic) {
		if (board_bind_eic_interrupt)
			board_bind_eic_interrupt (n, srs);
	} else if (cpu_has_vint) {
		/* SRSMap is only defined if shadow sets are implemented */
		if (mips_srs_max() > 1)
			change_c0_srsmap (0xf << n*4, srs << n*4);
	}

	if (srs == 0) {
		/*
		 * If no shadow set is selected then use the default handler
		 * that does normal register saving and a standard interrupt exit
		 */

		extern char except_vec_vi, except_vec_vi_lui;
		extern char except_vec_vi_ori, except_vec_vi_end;
#ifdef CONFIG_MIPS_MT_SMTC
		/*
		 * We need to provide the SMTC vectored interrupt handler
		 * not only with the address of the handler, but with the
		 * Status.IM bit to be masked before going there.
		 */
		extern char except_vec_vi_mori;
		const int mori_offset = &except_vec_vi_mori - &except_vec_vi;
#endif /* CONFIG_MIPS_MT_SMTC */
		const int handler_len = &except_vec_vi_end - &except_vec_vi;
		const int lui_offset = &except_vec_vi_lui - &except_vec_vi;
		const int ori_offset = &except_vec_vi_ori - &except_vec_vi;

		if (handler_len > VECTORSPACING) {
			/*
			 * Sigh... panicing won't help as the console
			 * is probably not configured :(
			 */
			panic ("VECTORSPACING too small");
		}

		memcpy (b, &except_vec_vi, handler_len);
#ifdef CONFIG_MIPS_MT_SMTC
		if (n > 7)
			printk("Vector index %d exceeds SMTC maximum\n", n);
		w = (u32 *)(b + mori_offset);
		*w = (*w & 0xffff0000) | (0x100 << n);
#endif /* CONFIG_MIPS_MT_SMTC */
		w = (u32 *)(b + lui_offset);
		*w = (*w & 0xffff0000) | (((u32)handler >> 16) & 0xffff);
		w = (u32 *)(b + ori_offset);
		*w = (*w & 0xffff0000) | ((u32)handler & 0xffff);
		flush_icache_range((unsigned long)b, (unsigned long)(b+handler_len));
	}
	else {
		/*
		 * In other cases jump directly to the interrupt handler
		 *
		 * It is the handlers responsibility to save registers if required
		 * (eg hi/lo) and return from the exception using "eret"
		 */
		w = (u32 *)b;
		*w++ = 0x08000000 | (((u32)handler >> 2) & 0x03fffff); /* j handler */
		*w = 0;
		flush_icache_range((unsigned long)b, (unsigned long)(b+8));
	}

	return (void *)old_handler;
}

void *set_vi_handler(int n, void *addr)
{
	return set_vi_srs_handler(n, addr, 0);
}

#else

static inline void mips_srs_init(void)
{
}

#endif /* CONFIG_CPU_MIPSR2_SRS */

/*
 * This is used by native signal handling
 */
asmlinkage int (*save_fp_context)(struct sigcontext *sc);
asmlinkage int (*restore_fp_context)(struct sigcontext *sc);

extern asmlinkage int _save_fp_context(struct sigcontext *sc);
extern asmlinkage int _restore_fp_context(struct sigcontext *sc);

extern asmlinkage int fpu_emulator_save_context(struct sigcontext *sc);
extern asmlinkage int fpu_emulator_restore_context(struct sigcontext *sc);

#ifdef CONFIG_SMP
static int smp_save_fp_context(struct sigcontext *sc)
{
	return cpu_has_fpu
	       ? _save_fp_context(sc)
	       : fpu_emulator_save_context(sc);
}

static int smp_restore_fp_context(struct sigcontext *sc)
{
	return cpu_has_fpu
	       ? _restore_fp_context(sc)
	       : fpu_emulator_restore_context(sc);
}
#endif

static inline void signal_init(void)
{
#ifdef CONFIG_SMP
	/* For now just do the cpu_has_fpu check when the functions are invoked */
	save_fp_context = smp_save_fp_context;
	restore_fp_context = smp_restore_fp_context;
#else
	if (cpu_has_fpu) {
		save_fp_context = _save_fp_context;
		restore_fp_context = _restore_fp_context;
	} else {
		save_fp_context = fpu_emulator_save_context;
		restore_fp_context = fpu_emulator_restore_context;
	}
#endif
}

#ifdef CONFIG_MIPS32_COMPAT

/*
 * This is used by 32-bit signal stuff on the 64-bit kernel
 */
asmlinkage int (*save_fp_context32)(struct sigcontext32 *sc);
asmlinkage int (*restore_fp_context32)(struct sigcontext32 *sc);

extern asmlinkage int _save_fp_context32(struct sigcontext32 *sc);
extern asmlinkage int _restore_fp_context32(struct sigcontext32 *sc);

extern asmlinkage int fpu_emulator_save_context32(struct sigcontext32 *sc);
extern asmlinkage int fpu_emulator_restore_context32(struct sigcontext32 *sc);

static inline void signal32_init(void)
{
	if (cpu_has_fpu) {
		save_fp_context32 = _save_fp_context32;
		restore_fp_context32 = _restore_fp_context32;
	} else {
		save_fp_context32 = fpu_emulator_save_context32;
		restore_fp_context32 = fpu_emulator_restore_context32;
	}
}
#endif

extern void cpu_cache_init(void);
extern void tlb_init(void);
extern void flush_tlb_handlers(void);

void __init per_cpu_trap_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int status_set = ST0_CU0;
#ifdef CONFIG_MIPS_MT_SMTC
	int secondaryTC = 0;
	int bootTC = (cpu == 0);

	/*
	 * Only do per_cpu_trap_init() for first TC of Each VPE.
	 * Note that this hack assumes that the SMTC init code
	 * assigns TCs consecutively and in ascending order.
	 */

	if (((read_c0_tcbind() & TCBIND_CURTC) != 0) &&
	    ((read_c0_tcbind() & TCBIND_CURVPE) == cpu_data[cpu - 1].vpe_id))
		secondaryTC = 1;
#endif /* CONFIG_MIPS_MT_SMTC */

	/*
	 * Disable coprocessors and select 32-bit or 64-bit addressing
	 * and the 16/32 or 32/32 FPR register model.  Reset the BEV
	 * flag that some firmware may have left set and the TS bit (for
	 * IP27).  Set XX for ISA IV code to work.
	 */
#ifdef CONFIG_64BIT
	status_set |= ST0_FR|ST0_KX|ST0_SX|ST0_UX;
#endif
	if (current_cpu_data.isa_level == MIPS_CPU_ISA_IV)
		status_set |= ST0_XX;
	change_c0_status(ST0_CU|ST0_MX|ST0_RE|ST0_FR|ST0_BEV|ST0_TS|ST0_KX|ST0_SX|ST0_UX,
			 status_set);

	if (cpu_has_dsp)
		set_c0_status(ST0_MX);

#ifdef CONFIG_CPU_MIPSR2
	write_c0_hwrena (0x0000000f); /* Allow rdhwr to all registers */
#endif

#ifdef CONFIG_MIPS_MT_SMTC
	if (!secondaryTC) {
#endif /* CONFIG_MIPS_MT_SMTC */

	/*
	 * Interrupt handling.
	 */
	if (cpu_has_veic || cpu_has_vint) {
		write_c0_ebase (ebase);
		/* Setting vector spacing enables EI/VI mode  */
		change_c0_intctl (0x3e0, VECTORSPACING);
	}
	if (cpu_has_divec) {
		if (cpu_has_mipsmt) {
			unsigned int vpflags = dvpe();
			set_c0_cause(CAUSEF_IV);
			evpe(vpflags);
		} else
			set_c0_cause(CAUSEF_IV);
	}
#ifdef CONFIG_MIPS_MT_SMTC
	}
#endif /* CONFIG_MIPS_MT_SMTC */

	cpu_data[cpu].asid_cache = ASID_FIRST_VERSION;
	TLBMISS_HANDLER_SETUP();

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);
	enter_lazy_tlb(&init_mm, current);

#ifdef CONFIG_MIPS_MT_SMTC
	if (bootTC)
#endif /* CONFIG_MIPS_MT_SMTC */
#ifdef CONFIG_DISCONTIGMEM
	/*
	 * on DISCONTIGMEM, these are done in setup_arch() on TP0 in order
	 * to create the extra kernel mappings for upper memory
	 */
	if(smp_processor_id() != 0)
#endif
	{
		cpu_cache_init();
		tlb_init();
	}
#ifdef CONFIG_MIPS_MT_SMTC
	else if (!secondaryTC) {
		/*
		 * First TC in non-boot VPE must do subset of tlb_init()
		 * for MMU countrol registers.
		 */
		write_c0_pagemask(PM_DEFAULT_MASK);
		write_c0_wired(0);
	}
#endif /* CONFIG_MIPS_MT_SMTC */
}

/* Install CPU exception handler */
void __init set_handler (unsigned long offset, void *addr, unsigned long size)
{
	memcpy((void *)(ebase + offset), addr, size);
	flush_icache_range(ebase + offset, ebase + offset + size);
}

/* Install uncached CPU exception handler */
void __init set_uncached_handler (unsigned long offset, void *addr, unsigned long size)
{
#ifdef CONFIG_32BIT
	unsigned long uncached_ebase = KSEG1ADDR(ebase);
#endif
#ifdef CONFIG_64BIT
	unsigned long uncached_ebase = TO_UNCAC(ebase);
#endif

	memcpy((void *)(uncached_ebase + offset), addr, size);
}

void __init trap_init(void)
{
	extern char except_vec3_generic, except_vec3_r4000;
	extern char except_vec4;
	unsigned long i;

#if defined(CONFIG_KGDB)
	if (kgdb_early_setup)
		return;	/* Already done */
#endif

	if (cpu_has_veic || cpu_has_vint)
		ebase = (unsigned long) alloc_bootmem_low_pages (0x200 + VECTORSPACING*64);
	else
		ebase = CAC_BASE;

	mips_srs_init();

	per_cpu_trap_init();

	/*
	 * Copy the generic exception handlers to their final destination.
	 * This will be overriden later as suitable for a particular
	 * configuration.
	 */
	set_handler(0x180, &except_vec3_generic, 0x80);

	/*
	 * Setup default vectors
	 */
	for (i = 0; i <= 31; i++)
		set_except_vector(i, handle_reserved);

	/*
	 * Copy the EJTAG debug exception vector handler code to it's final
	 * destination.
	 */
	if (cpu_has_ejtag && board_ejtag_handler_setup)
		board_ejtag_handler_setup ();

	/*
	 * Only some CPUs have the watch exceptions.
	 */
	if (cpu_has_watch)
		set_except_vector(23, handle_watch);

	/*
	 * Initialise interrupt handlers
	 */
	if (cpu_has_veic || cpu_has_vint) {
		int nvec = cpu_has_veic ? 64 : 8;
		for (i = 0; i < nvec; i++)
			set_vi_handler(i, NULL);
	}
	else if (cpu_has_divec)
		set_handler(0x200, &except_vec4, 0x8);

	/*
	 * Some CPUs can enable/disable for cache parity detection, but does
	 * it different ways.
	 */
	parity_protection_init();

	/*
	 * The Data Bus Errors / Instruction Bus Errors are signaled
	 * by external hardware.  Therefore these two exceptions
	 * may have board specific handlers.
	 */
	if (board_be_init)
		board_be_init();

	set_except_vector(0, handle_int);
	set_except_vector(1, handle_tlbm);
	set_except_vector(2, handle_tlbl);
	set_except_vector(3, handle_tlbs);

	set_except_vector(4, handle_adel);
	set_except_vector(5, handle_ades);

	set_except_vector(6, handle_ibe);
	set_except_vector(7, handle_dbe);

	set_except_vector(8, handle_sys);
	set_except_vector(9, handle_bp);
	set_except_vector(10, handle_ri);
	set_except_vector(11, handle_cpu);
	set_except_vector(12, handle_ov);
	set_except_vector(13, handle_tr);

	if (current_cpu_data.cputype == CPU_R6000 ||
	    current_cpu_data.cputype == CPU_R6000A) {
		/*
		 * The R6000 is the only R-series CPU that features a machine
		 * check exception (similar to the R4000 cache error) and
		 * unaligned ldc1/sdc1 exception.  The handlers have not been
		 * written yet.  Well, anyway there is no R6000 machine on the
		 * current list of targets for Linux/MIPS.
		 * (Duh, crap, there is someone with a triple R6k machine)
		 */
		//set_except_vector(14, handle_mc);
		//set_except_vector(15, handle_ndc);
	}


	if (board_nmi_handler_setup)
		board_nmi_handler_setup();

	if (cpu_has_fpu && !cpu_has_nofpuex)
		set_except_vector(15, handle_fpe);

	set_except_vector(22, handle_mdmx);

	if (cpu_has_mcheck)
		set_except_vector(24, handle_mcheck);

	if (cpu_has_mipsmt)
		set_except_vector(25, handle_mt);

	if (cpu_has_dsp)
		set_except_vector(26, handle_dsp);

	if (cpu_has_vce)
		/* Special exception: R4[04]00 uses also the divec space. */
		memcpy((void *)(CAC_BASE + 0x180), &except_vec3_r4000, 0x100);
	else if (cpu_has_4kex)
		memcpy((void *)(CAC_BASE + 0x180), &except_vec3_generic, 0x80);
	else
		memcpy((void *)(CAC_BASE + 0x080), &except_vec3_generic, 0x80);

	signal_init();
#ifdef CONFIG_MIPS32_COMPAT
	signal32_init();
#endif

	flush_icache_range(ebase, ebase + 0x400);
	flush_tlb_handlers();
}

#ifdef CONFIG_TIVO
/**
 * To stop and start runnables not needed, since we are getting the 
 * backtrace for just current task.
 * 
 * The name of the API reflects does not have 'tivo' in it since
 * it will be called from Broadcom code base.
 */
void tv_show_trace_task_bt(struct task_struct *tsk, int debugconsole) 
{
	if(in_interrupt()) {
		printk("Can't invoke %s in interrupt context\n", __FUNCTION__);
		return;
	}

	dump_module_addrs();

	if (!tsk->mm) {
		printk("No user context exists. kernel trace is: \n");
	}

	show_trace_task_bt(tsk,debugconsole);
}
#endif

void show_trace_task_bt(struct task_struct *tsk, int dispconsole)
{
	unsigned int *pc, *sp, *fp, *ra;
	struct pt_regs *user_regs;

	if (tsk == current) {
		asm (" move %0,$29": "=r" (sp));
		asm (" move %0,$30": "=r" (fp));
		asm (" move %0,$31": "=r" (ra));
		asm ("9: la %0,9b": "=r" (pc));
	} else {
		struct thread_struct *t = &tsk->thread;

		// Uhg, horribly compiler (and optimization level) dependant
		// see schedule()'s stack frame:
		ra = pc = (unsigned int *) t->reg31;
		sp = (unsigned int *) t->reg29;
		fp = (unsigned int *) t->reg30;
	}
	if(dispconsole == 0)
		printk(KERN_INFO);
	printk("      ");
        user_regs = _trace_kern(pc, sp, fp, ra, dispconsole,tsk);
        printk("\n");
        if ( user_regs){
                _trace_user(user_regs, dispconsole,tsk);
        }
}

#ifdef CONFIG_TIVO
EXPORT_SYMBOL(show_trace_task_bt);
EXPORT_SYMBOL(tv_show_trace_task_bt);
#endif

/*
 * This makes gross assumptions about compiler behavior, but it's better
 * than just blindly scanning the stack for addresses that look like
 * they might be in kernel functions...
 */

#define FRAME_IBUF_SZ 2048*4
static int process_frame(unsigned int **ppc, unsigned int **psp,
                         unsigned int **pfp, unsigned int **pra,
                         struct task_struct *tsk,int user_stack)
{
	unsigned int *pc = *ppc, *sp = *psp, *fp = *pfp, *ra = *pra;
	unsigned int *saved_fp_pos = NULL, *saved_fp_val = NULL;
	unsigned int *saved_ra_val = NULL, *saved_ra_pos = NULL;
	unsigned int *lowpc = pc, *oldpc = pc, *oldsp = sp;
	signed short offset;
	unsigned int inst, previnst;
	int i;
        unsigned int tpc;
        unsigned int *frame_ibuf=NULL;
        int retval = 0;
        
        if ( user_stack && (tsk != current) ){
                frame_ibuf = (unsigned int)kmalloc(FRAME_IBUF_SZ,GFP_KERNEL);
                if ( !frame_ibuf ) {
                        retval = -ENOMEM;
                        goto out;
                }
                if ( !access_process_vm(tsk, (pc-1), frame_ibuf, FRAME_IBUF_SZ, 0) ){
                        retval = -EFAULT;
                        goto out;
                }
                tpc = (unsigned int)(pc-1);
        }

	// an arbitrary limit to function size...
	i = 2048;
	while (1) {
		if (--i == 0) {
                        retval = -EAGAIN;
                        goto out;
		}
                if ( user_stack && (tsk != current)){
                        if( pc < tpc || pc >= (tpc + FRAME_IBUF_SZ)){
                                if ( !access_process_vm(tsk, pc, frame_ibuf, FRAME_IBUF_SZ, 0) ){
                                        retval = -EFAULT;
                                        goto out;
                                }
                                tpc = pc;
                        }
                        inst = frame_ibuf[((unsigned int)pc - tpc)/4];
                }
                else {
                        if (__get_user(inst, pc)){
                                retval = -EFAULT;
                                goto out;
                        }
                }
		if ((inst == 0x03e00008) || (inst == 0x03200008))  { // jr ra or jr t9
			break;
		}
		else if (inst == 0x42000018) { // eret
			*ppc = 0;
			*psp = sp;
			*pfp = fp;
			*pra = (unsigned int *)0xffffffff;
			return 0;
		}
		else if ((inst & 0xffff0000) == 0x27bd0000) { // addiu sp, sp, n
			offset = (short)((long)inst & 0xffff);
			sp = (unsigned int *)((long)sp + offset);
		}
		else if (inst == 0x03a0f021) { // move fp, sp
			fp = sp;
		}
		else if (inst == 0x03c0e821) { // move sp, fp
			sp = fp;
		}
		else if ((inst & 0xffff0000) == 0x8fbf0000) { // lw ra, n(sp)
			unsigned int *ra_pos;
			offset = (short)((long)inst & 0xffff);
			ra_pos = (unsigned int *)((long)sp + offset);
			if (ra_pos == saved_ra_pos)
				ra = saved_ra_val;
			else {
                                if ( user_stack && (tsk != current) ){
                                        if ( !access_process_vm(tsk, ra_pos, &ra, 4, 0) ){
                                                retval = -EFAULT;
                                                goto out;
                                        }
                                }
                                else{
                                        if (__get_user(ra, ra_pos)){
                                                retval = -EFAULT;
                                                goto out;
                                        }
                                }
                        }
		}
		else if ((inst & 0xffff0000) == 0x8fbe0000) { // lw fp, n(sp)
			unsigned int *fp_pos;
			offset = (short)((long)inst & 0xffff);
			fp_pos = (unsigned int *)((long)sp + offset);
			if (fp_pos == saved_fp_pos)
				fp = saved_fp_val;
			else {
                                if ( user_stack && (tsk != current) ){
                                        if ( !access_process_vm(tsk, fp_pos, &fp, 4, 0) ){
                                                retval = -EFAULT;
                                                goto out;
                                        }
                                }
                                else {
                                        if (__get_user(fp, fp_pos)){
                                                retval = -EFAULT;
                                                goto out;
                                        }
                                }
                        }
		}
		// if we see ra or fp being saved onto stack, just note it,
		// and use that value when it's read back off stack
		else if ((inst & 0xffff0000) == 0xafbf0000) { // sw ra, n(sp)
			offset = (short)((long)inst & 0xffff);
			saved_ra_val = ra;
			saved_ra_pos = (unsigned int *)((long)sp + offset);
		}
		else if ((inst & 0xffff0000) == 0xafbe0000) { // sw fp, n(sp)
			offset = (short)((long)inst & 0xffff);
			saved_fp_val = fp;
			saved_fp_pos = (unsigned int *)((long)sp + offset);
		}
		// Handle jump backwards to return statement.
                if ( user_stack && (tsk != current)){
                        if( (pc-1) < tpc || (pc-1) >= (tpc + FRAME_IBUF_SZ)){
                                if ( !access_process_vm(tsk, (pc-1), frame_ibuf, FRAME_IBUF_SZ, 0) ){
                                        retval = -EFAULT;
                                        goto out;
                                }
                                tpc = (pc-1);
                        }
                        previnst = frame_ibuf[((unsigned int)(pc-1) - tpc)/4];
                }
                else {
                        if (__get_user(previnst, pc - 1)){
                                retval = -EFAULT;
                                goto out;
                        }
		}
		// try to detect exit syscall
		if (inst == 0x0000000c && (previnst & 0xffff) == __NR_exit) {
			*ppc = 0;
			*psp = sp;
			*pfp = fp;
			*pra = 0;
			return 0;
		}
		if ((previnst & 0xb0000000) == 0x10000000 ||
		    (previnst & 0xfc0c0000) == 0x04000000) {
			offset = (short)((long)previnst & 0xffff);
			if (pc + offset < lowpc) {
				pc = pc + offset;
				lowpc = pc;
				pc--;
			}
		}
		else if ((previnst & 0xfc000000) == 0x08000000) {
			unsigned long newpc;

			newpc =((unsigned long)previnst & 0x03ffffff) << 2;
			newpc |= (unsigned long)pc & 0xf0000000;

			if ((unsigned int *)newpc < lowpc) {
				pc = (unsigned int *)newpc;
				lowpc = pc;
				pc--;
			}
		}
		pc++;
	}
	// handle the delay slot, where the sp adjustment usually is
        if ( user_stack && (tsk != current) ){
                if (!access_process_vm(tsk, (pc+1), &inst, 4, 0)){
                        retval = -EFAULT;
                        goto out;
                }
        }
        else {
                if (__get_user(inst, pc + 1)){
                        retval = -EFAULT;
                        goto out;
                }
	}
	if ((inst & 0xffff0000) == 0x27bd0000) { // addiu sp, sp, n
		offset = (short)((long)inst & 0xffff);
		sp = (unsigned int *)((long)sp + offset);
	}
	pc = ra;
	// try to avoid looping forever if we recurse with no (or bad) sp
	// adjustment
	if (pc == oldpc && sp <= oldsp) {
		pc = 0;
	}
	*ppc = pc;
	*psp = sp;
	*pfp = fp;
	*pra = ra;
out:
        if (frame_ibuf)
                kfree(frame_ibuf);
	return retval;
}

static struct pt_regs * _trace_kern(unsigned int *pc, unsigned int *sp,
                                    unsigned int *fp, unsigned int *ra,
				    int dispconsole,struct task_struct *tsk)
{
	int i = 0;

	while (pc != 0) {
		if (((long)pc & 3) || (unsigned long)pc < KSEG0 ||
		    ((long)sp & 3) || (unsigned long)sp < KSEG0) {
			if (i == 0)
				printk(" (none)");
			break;
		}

		printk(" %08lx", (unsigned long)pc);

		if (++i % 8 == 0) {
			printk("\n");
			if(dispconsole == 0)
				printk(KERN_INFO);
			printk("      ");
		}
		if (i > 40 ) {
			printk(" ...");
			break;
		}

		if (process_frame(&pc, &sp, &fp, &ra,tsk,0)) {
			printk(" (Bad trace)");
			break;
		}
		if (pc == 0 && ra == (unsigned int *)0xffffffff) {
			struct pt_regs *regs = (struct pt_regs *)sp;
#ifdef CONFIG_CISR_STACK
			extern u8 isr_stack[16 * 1024];

			/*
			 * The assembler code in ret_from_irq is incorrectly
			 * handled for the case that the stack frame is
			 * on the interrupt stack, so ajdust for that here.
			 */
			if (regs >= isr_stack && regs < isr_stack + 16 * 1024) {
				regs = (struct pt_regs *)
				((unsigned long)regs + sizeof(struct pt_regs));
			}
#endif

			printk(" ");

			if (regs->cp0_epc >= KSEG0) {
				pc = (unsigned int *)regs->cp0_epc;
				sp = (unsigned int *)regs->regs[29];
				fp = (unsigned int *)regs->regs[30];
				ra = (unsigned int *)regs->regs[31];
			} else {
				//printk(" %08lx", (unsigned long)regs->cp0_epc);
				return regs;
			}
		}
	}
	return NULL;
}

static void _trace_user(struct pt_regs * regs, int dispconsole, 
                        struct task_struct *tsk)
{
	unsigned int *pc, *sp, *fp, *ra;
	int i = 0;

	pc = (unsigned int *)regs->cp0_epc;
	sp = (unsigned int *)regs->regs[29];
	fp = (unsigned int *)regs->regs[30];
	ra = (unsigned int *)regs->regs[31];

	if(dispconsole == 0)
		printk(KERN_INFO);
	printk("      ");

	while (pc != 0) {
		if (((long)pc & 3) || ((long)sp & 3)) {
			if (i == 0)
				printk(" (none)");
			break;
		}

		printk(" %08lx", (unsigned long)pc);

		if (++i % 8 == 0) {
			printk("\n");
			if(dispconsole == 0)
				printk(KERN_INFO);
			printk("      ");
		}

		if (i > 40 ) {
			printk(" ...");
			break;
		}

		// Note that this will fail if the virtual address is in a
		// non-present page, and this was called at interrupt context,
		// but it shouldn't blow up...
		if (process_frame(&pc, &sp, &fp, &ra,tsk,1)) {
			printk(" (Bad trace)");
			break;
		}
	}
	printk("\n");

}

struct pt_regs * show_trace_task_regs(struct task_struct *tsk, int dispconsole)
{
	unsigned int *pc, *sp, *fp, *ra;
	struct pt_regs *user_regs;

	if (tsk == current) {
		asm (" move %0,$29": "=r" (sp));
		asm (" move %0,$30": "=r" (fp));
		asm (" move %0,$31": "=r" (ra));
		asm ("9: la %0,9b": "=r" (pc));
	} else {
		struct thread_struct *t = &tsk->thread;

		// Uhg, horribly compiler (and optimization level) dependant
		// see schedule()'s stack frame:
#if 0
                /* we derived following from 2.4 kernel. But it is broken in 2.6 kernel */                
		ra = pc = (unsigned int *)thread_saved_pc(tsk);
		sp = (unsigned int *)((unsigned long)t->reg30 + 56);
		fp = (unsigned int *)((unsigned long *)t->reg30)[11];
#endif                
		ra = pc = (unsigned int *) t->reg31;
		sp = (unsigned int *) t->reg29;
		fp = (unsigned int *) t->reg30;
	}
	if(dispconsole == 0)
		printk(KERN_INFO);
	printk("      ");
        user_regs = _trace_kern(pc, sp, fp, ra, dispconsole,tsk);
        printk("\n");
        if ( user_regs  && tsk == current){
                _trace_user(user_regs, dispconsole,tsk);
        }

	return user_regs;
}

void show_trace_task(struct task_struct *tsk)
{
	show_trace_task_regs(tsk, 1);
}
