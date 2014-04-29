/*
 * crashallassrt: Walks all the tasks' stacks, both kernel and user,
 * inorder to capture the back traces
 *
 * Copyright (C) 2005, 2006 TiVo Inc. All Rights Reserved.
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
#include <linux/fastbufs.h>
#include <linux/reboot.h>
#include <linux/dcache.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include <linux/fs.h>

extern void emergency_sync(void);
extern void show_trace_task_bt(struct task_struct *tsk, int dispconsole);
extern void do_emergency_sync(void);
extern void _machine_restart(char *command);

extern struct list_head modules;

extern void stop_runnables(void);
void restore_runnables(void);
static ssize_t pid_read_maps (struct task_struct *, char *);
void dump_module_addrs(void);

static inline pid_t process_group(struct task_struct *tsk)
{
    /* XXX: Is this correct? */
    /* return tsk->signal->pgrp; */
    return pid_nr(task_pgrp(tsk));
}

/*
 * The max number is PID_MAX defined in linux/threads.h
 */
#define THREAD_MAP_BLKS         (PID_MAX_DEFAULT/(sizeof(u_long)*8))
u_int *tgl_bit_map=NULL;

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

void restore_runnables(){
        struct task_struct *tsk; 

        for_each_process(tsk) {
                if (tsk->mm && 
                        tsk != &init_task &&
                        tsk != current &&
                        strcmp(tsk->comm,"syslogd") &&
                        strcmp(tsk->comm,"klogd") && 
                        strcmp(tsk->comm,"init")) {

                        atomic_set(&tsk->allowed_to_run,1);
                        wake_up_process(tsk);
                }
        }
}

int tivocrashallassert(unsigned long arg){
        struct task_struct *tsk; 
        char *tbuf;
        struct k_arg_list k_arg_list;
        struct k_arg *k_args,*k_argsp;
        unsigned int time_stamp = 0xdeadbeaf;
        int err;
        int count;
        int tmkdbg=0;
        int retval = 0;
        u_int *tmp;
        int seg;
        int shift;
        thread_fbuf *exec_cntxt;

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

        tgl_bit_map =(u_int *)kmalloc(THREAD_MAP_BLKS,GFP_KERNEL);
        if ( tgl_bit_map == NULL ){
                retval = -ENOMEM;
                goto out;
        }

        /*
        *Prevent almost all runnable threads not to get scheduled on cpu
        */ 
	read_lock_irq(&tasklist_lock);
        stop_runnables();
	read_unlock_irq(&tasklist_lock);

        printk ( DEBUG_LEVEL "bt_all_thread 0x%08x start\n",time_stamp);

        printk( DEBUG_LEVEL "global\n");

        dump_module_addrs();

        /*
         *Find thread group leaders
         */ 
        read_lock(&tasklist_lock);
        for_each_process(tsk) {
                tmp = &tgl_bit_map[process_group(tsk)/(sizeof(u_long)*8)];
                *tmp  |= 1<<(process_group(tsk)%(sizeof(u_long)*8));
        }
        read_unlock(&tasklist_lock);

    for ( tmp = &tgl_bit_map[0], seg=0; seg < THREAD_MAP_BLKS; tmp++,seg++){
        shift = 0;
        while( shift < sizeof(u_long)*8 ) {
            if ( *tmp & (1<<shift) ) {
                int pgrp = shift+seg*sizeof(u_long)*8;
                int leader = 1;    
                for_each_process(tsk){
                    if ( pgrp == process_group(tsk)){
                        if ( leader){
                            printk(DEBUG_LEVEL "threadgroup %d name \"%s\"\n",
                                                        process_group(tsk), tsk->comm);

                            /*
                             *Dump memory map of a valid threadgroup
                            */ 
                            pid_read_maps(tsk, tbuf);
                            leader = 0;
                        }
                                                
                        printk(DEBUG_LEVEL "pid %d threadgroup %d name \"%s",
                            tsk->pid, 
                            process_group(tsk),
                            tsk->comm);

                        exec_cntxt = (thread_fbuf *) tsk->exec_cntxt;
                        if ( exec_cntxt && exec_cntxt->cntxt )
                            printk(DEBUG_LEVEL "-%s\"\n",exec_cntxt->cntxt->name);
                        else
                            printk(DEBUG_LEVEL "\"\n");
                                
                        /*
                         *Dump back traces 
                         */ 
                        show_trace_task_bt(tsk,debugconsole);

                        /*let other threads like syslogd run*/
                        /* BUG NO: 209100 - 
                           There has been log buffer overrun when printing lots of logs especially 
                           with crash all assert. Solution is to allow syslogd and klogd for longer 
                           period by using schedule_timeout in INTERRUPTIBLE mode. Timeout 100 jiffies.
                        */
                         set_current_state(TASK_INTERRUPTIBLE);
                         schedule_timeout(100);
                    }
                }
            }
            shift++;
        }
    }

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
out:
    if (tgl_bit_map)
        kfree(tgl_bit_map);
    return retval;
}

void dump_module_addrs(void)
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
