/*
 * crashallassert: Walks all the tasks' stacks, both kernel and user,
 * inorder to capture the back traces
 *
 * Copyright (C) 2011 TiVo Inc. All Rights Reserved.
 */

#include <linux/string.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/user.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/tivo-crashallassert.h>
#include <linux/reboot.h>
#include <linux/dcache.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <asm-generic/delay.h>

extern void emergency_sync(void);
extern void show_trace_task_bt(struct task_struct *tsk, int dispconsole);
extern void do_emergency_sync(void);
extern void _machine_restart(char *command);

extern struct list_head modules;

// In sched.c
extern int stop_runnables(void);
extern void restore_runnables(void);

static ssize_t pid_read_maps (struct task_struct *, char *);
static void dump_module_addrs(void);

static inline pid_t process_group(struct task_struct *tsk)
{
    /* XXX: Is this correct? */
    /* return tsk->signal->pgrp; */
    return pid_nr(task_pgrp(tsk));
}

static int debugconsole = 1;

#define DEBUG_LEVEL KERN_ERR

static ssize_t pid_read_maps (struct task_struct *task, char *tmp)
{
	struct mm_struct *mm;
	struct vm_area_struct * map;
	long retval;
	char *line;

	task_lock(task);
	mm = task->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);

	retval = 0;
	if (!mm)
		return retval;

	down_read(&mm->mmap_sem);
	map = mm->mmap;

	while (map) {
                if ( (map->vm_file == NULL) || 
                        !((map->vm_flags & VM_READ) && 
                        (map->vm_flags & VM_EXEC))) { 

                        map = map->vm_next;
                        continue;
                }
                line = d_path(&map->vm_file->f_path, tmp, PAGE_SIZE);
                printk( DEBUG_LEVEL "read 0x%08lx 0x%08lx %s\n",
                                    map->vm_start,map->vm_end,line);
                map = map->vm_next;
        }
	up_read(&mm->mmap_sem);
	mmput(mm);

	return retval;
}

#define MAX_STOP_RUN_ATTEMPTS                (100)

int tivocrashallassert(unsigned long arg){
        struct task_struct *g, *p;
        char *tbuf;
        struct k_arg_list k_arg_list;
        struct k_arg *k_args,*k_argsp;
        unsigned int time_stamp = 0xdeadbeaf;
        int err;
        int count;
        int tmkdbg=0;
        unsigned long flush_count = 0;

        err = copy_from_user((void *)&k_arg_list,
                    (void *)arg,sizeof(struct k_arg_list));
        if ( err )
                return -EFAULT;

        k_args = (struct k_arg *)
                kmalloc(sizeof(struct k_arg)*k_arg_list.count,GFP_KERNEL);
        if ( k_args == NULL )
                return -ENOMEM;

        err = copy_from_user((void *)k_args,
                            (void *)k_arg_list.kargs,
                            sizeof(struct k_arg)*k_arg_list.count);
        if (err){
                kfree(k_args);
                return -EFAULT;
        }

        for ( count=0,k_argsp=k_args; count < k_arg_list.count; k_argsp++,count++){
        switch(k_argsp->tag){
            case TAG_TIME:
                time_stamp = k_argsp->ptr;
            break;
            case TAG_TMKDBG:
                if ( k_argsp->ptr ) 
                    tmkdbg = 1;
                else
                    tmkdbg = 0; 
            break; 
            default:
                printk( DEBUG_LEVEL "Invalid tag/ptr/len\n");
        }
        }

        kfree(k_args);

        tbuf = (char*)__get_free_page(GFP_KERNEL);
        if (!tbuf)
                return -ENOMEM;

        /*
        * Prevent almost all runnable threads from getting scheduled on a CPU.
        * However, we would be waiting for the thread currently running on a different CPU
        * to relinquish the CPU before deactivating it.
        */
        for (count=0;count<MAX_STOP_RUN_ATTEMPTS;count++) {
            read_lock_irq(&tasklist_lock);
            err = stop_runnables();
            read_unlock_irq(&tasklist_lock);
            if(!err) {
                break;
            } else {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(1); /* jiffies */
            }
        }

        if(count == MAX_STOP_RUN_ATTEMPTS) {
            printk(DEBUG_LEVEL "Failed to stop pid=%d\n",err);
        }

        printk ( DEBUG_LEVEL "bt_all_thread 0x%08x start\n",time_stamp);

        printk( DEBUG_LEVEL "global\n");

        dump_module_addrs();

        read_lock_irq(&tasklist_lock);
        do_each_thread(g, p) {
            if( !(++flush_count % 5) ) {
                read_unlock_irq(&tasklist_lock);
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(100);
                read_lock_irq(&tasklist_lock);
            }

            if (p == g) {
				printk(DEBUG_LEVEL "threadgroup %d name \"%s\"\n", g->tgid, g->comm);
                /*Dump memory map of a valid threadgroup*/
                pid_read_maps(g, tbuf);
            }

			printk(DEBUG_LEVEL "pid %d threadgroup %d name \"%s", p->pid, p->tgid, p->comm);
            printk(DEBUG_LEVEL "\"\n");
                                                                                                                                                                                  /*Dump back traces*/
            show_trace_task_bt(p, debugconsole);

        } while_each_thread(g, p);
        read_unlock_irq(&tasklist_lock);

        printk ( DEBUG_LEVEL "bt_all_thread 0x%08x end\n",time_stamp);

        free_page((unsigned long)tbuf);

        /*
        * Let runnable threads get scheduled as usual but with 
        * rt scheduler still turned off
        */ 
        read_lock_irq(&tasklist_lock);
        restore_runnables();
        read_unlock_irq(&tasklist_lock);

        /*
         * flush all disk buffers
         */ 
        emergency_sync();

        if ( !tmkdbg ){
                /* 
                 * In release kernel, wait for sometime(50 sec) before rebooting
                 * to ensure that crashall is logged in the log files.
                 */
                set_current_state(TASK_UNINTERRUPTIBLE);
                schedule_timeout(50*HZ);
                
                /*
                 *Done dumping back traces. Reboot the TCD
                 */ 
                machine_restart(NULL);
    }

    return 0;
}

static void dump_module_addrs(void)
{
        int fd;
        struct module *mod;
        char mod_path[256];
        char mod_name[MODULE_NAME_LEN];
        mm_segment_t old_fs;

        old_fs = get_fs();
        set_fs(KERNEL_DS);

        /*Kernel Loadable Module information*/
        list_for_each_entry(mod, &modules, list) {

                strcpy(mod_name,mod->name);

                strcat(mod_name,".ko");
                strcpy(mod_path,"/platform/lib/modules/");
                strcat(mod_path,mod_name);

                fd = sys_open((char __user *)mod_path, 0, 0);

                if(fd < 0) {
                        strcpy(mod_path,"/lib/modules/");
                        strcat(mod_path,mod_name);
                        fd = sys_open((char __user *)mod_path, 0, 0);
                }

                if(fd >= 0){               
                        sys_close(fd);
                        printk(DEBUG_LEVEL "read 0x%p %s\n", mod->module_core , mod_path);
                }
        }

        set_fs(old_fs);
}

#ifdef CONFIG_TICK_KILLER
int tick_killer_start(int ticks) {
	if(ticks >= 0) {
		if(tick_killer_ticks == ticks) {
			printk("TickKiller called with %d, no effect\n", ticks);
			return 0;
		}
		if(ticks == TICK_KILLER_DISABLED) {
			printk("TickKiller disabled\n");
		} else {
			printk("TickKiller will kill the TCD in %d ticks\n",
					ticks);
		}
		tick_killer_ticks = ticks;
	} else {
		return -EINVAL;
	}
	return 0;
}
#endif
