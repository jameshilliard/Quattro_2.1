/**
 * File: plog.c
 * 
 * "Process log" for 2.6 SMP kernel.
 * 
 * Implementation notes:
 * 
 * 1. PLOG_26_XXX is used to commented out not yet ported to 2.6 kernel code.
 * 
 * 2. A spinlock is used to insure multithreading safety.
 * 
 * 3. Pairs of {spin_lock_irqsave() | spin_unlock_irqrestore()} are used even
 *    from ioctl handling code, where {spin_lock_irq() | spin_unlock_irq()}
 *    will be fine as well. Code simplicity, may be?
 * 
 * 4. The plog event header has been changed from 2.4 version.
 * 
 * 5. New plog_syscall_start() and plog_syscall_end() have replaced original
 *    plog_sc_enter() and plog_sc_leave() respectively.
 * 
 * Copyright (C) 2001-2009 TiVo, Inc. All Rights Reserved.
 */

#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/plog.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/tivoconfig.h>
#include <linux/blkdev.h>
#include <linux/tivo-private.h>
#include <linux/tivo-monotime.h>
#include <linux/poll.h>
#include <linux/spinlock.h> 

#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/semaphore.h>

extern int mem_get_contigmem_region(unsigned int   region, 
                                    unsigned long *start,
                                    unsigned long *size);

// On SMP we need a spinlock to make this multithread safe.
static spinlock_t s_lock = SPIN_LOCK_UNLOCKED;

// Only one person can read from plog at a time ... we could easily create a
// read pointer per file (using file->private_data) ... but we can't easily
// invalidate all of those pointers when the circular log gets overwritten.
// There is probably an easy solution to this ... I'm just not thinking hard
// enough... KPS 6/7/05
//
static int s_plogOpenedForReading = 0;

/**
 * Event header:
 *
 * The bits of the 1st 32 bit word are used as follows:
 * 
 *  [32]     CPU core ID, 0 or 1
 *  [31-16]  predefined type of plog entry
 *  [15-0]   length of plog entry
 * 
 * The second 32 bit word is used for timestamp value, produced as lower 32 
 * bits of system-wide clock.
 * 
 * Where: the "system-wide clock" is not CPU core bounded HW 27 Mhz cycle 
 *        counter for 7405.
 */

// This define controls whether plog is allocated
// as a static array or as a contigmem region.
//
//#define USE_STATIC_PLOG

#ifndef USE_STATIC_PLOG

static int *s_processLog = NULL;
static unsigned long s_processLogSizeBytes = 0;

#define PLOG_LENGTH s_processLogSizeBytes

/**
 * Leave plog disabled until module initialization time.
 * 
 * If you need to debug system bootup with plog, define
 * USE_STATIC_PLOG
 */

static int s_plog_disable = 1;

#else // USE_STATIC_PLOG

static int s_processLog[32768];
#define PLOG_LENGTH sizeof(s_processLog)
static int s_plog_disable = 0;

#endif // !USE_STATIC_PLOG

#define MAX_EVENT   8192

// A 1 in this mask means that the event should not be logged.
static int s_ptypeMask[MAX_EVENT >> 5];

// The size of a "safety zone" between the write pointer and the read pointer.
// This zone is to mostly insure that when plog_read() tries to read the buffer
// and re-enables interrupts that the place in the buffer that it starts
// reading from doesn't immediately get overwritten by things getting logged.
//
#define PLOG_SAFETY_ZONE       1024

#define PLOG_HEADER_SIZE       8

#define PLOG_CONTIGMEM_REGION  0

/* Read/Write pointers are in bytes */
static volatile int s_plog_wp = 0;
static volatile int s_plog_rp = 0;

static wait_queue_head_t plog_wait;

// Anyone can open for writing, and only one for reading.
//
// We can do better, see KPS comment above.
//
static int plog_open(struct inode * inode, struct file * file)
{
    unsigned int f_flags = file->f_flags & O_ACCMODE;

    if (f_flags == O_RDONLY || f_flags == O_RDWR)
    {
        if (s_plogOpenedForReading)
        {
            return -EBUSY;
        }
        s_plogOpenedForReading = 1;
    }

    return 0;
}

static int plog_release(struct inode * inode, struct file * file)
{
    unsigned int f_flags = file->f_flags & O_ACCMODE;

    if (f_flags == O_RDONLY || f_flags == O_RDWR)
    {
        s_plogOpenedForReading = 0;
    }

    return 0;
}

static ssize_t plog_read(struct file *file, char *buf, size_t len, loff_t *off)
{
    int            error = -EINVAL;
    int            eventLen;
    volatile int   readable;
    int           *s;
    int            i;
    int           *userBuf;
    unsigned long  flags;
    unsigned int   f_flags = file->f_flags & O_ACCMODE;

    if (f_flags != O_RDONLY && f_flags != O_RDWR)
    {
        goto out;
    }

    // The user buffer *must* be word aligned for efficiency (would hate to
    // slow things down significantly as we are trying to profile them).
    if (!buf || len < 0) 
    {
        goto out;
    }

    if (((int) buf) & 0x3) 
    {
        goto out;
    }

    if (!len) 
    {
        goto out;
    }

    if (!access_ok(VERIFY_WRITE,buf,len))
    {
        goto out;
    }

    userBuf = (int *) buf;

    spin_lock_irqsave(&s_lock, flags);

    if (s_plog_wp == PLOG_LENGTH) 
    {
        s_plog_wp = 0;
    }

    error = -ERESTARTSYS;
    if (s_plog_wp >= s_plog_rp) 
    {
        readable = s_plog_wp - s_plog_rp;
    } 
    else 
    {
        readable = s_plog_wp + (PLOG_LENGTH - s_plog_rp);
    }

    // Must have 4K available before we let them read anything.
    while (readable < 4096) 
    {
        spin_unlock_irqrestore(&s_lock, flags);

        if (signal_pending(current)) 
        {
            goto out;
        }

        interruptible_sleep_on(&plog_wait);

        spin_lock_irqsave(&s_lock, flags);

        if (s_plog_wp >= s_plog_rp) 
        {
            readable = s_plog_wp - s_plog_rp;
        } 
        else 
        {
            readable = s_plog_wp + (PLOG_LENGTH - s_plog_rp);
        }
    }

    i = 0;

    while (s_plog_rp != s_plog_wp && len > 0) 
    {
        eventLen = s_processLog[s_plog_rp >> 2] & 0xffff;
        if (eventLen == 4) 
        {
            // null event
            s_plog_rp += 4;
            if (s_plog_rp == PLOG_LENGTH) 
            {
                s_plog_rp = 0;
            }
            continue;
        }

        if (len >= eventLen) 
        {
            len -= eventLen;

            s = s_processLog + (s_plog_rp >> 2);

            s_plog_rp += eventLen;

            if (s_plog_rp == PLOG_LENGTH) 
            {
                s_plog_rp = 0;
            }

            i += eventLen;

            /**
             * Releasing spinlock during copying data to user space,
             * a physical page may not be there.
             * 
             * TBD: use copy_to_user() here.
             */

            spin_unlock_irqrestore(&s_lock, flags);
            while (eventLen > 0) 
            {
                __put_user(*s, userBuf);
                eventLen -= 4;
                s++;
                userBuf++;
            }
            spin_lock_irqsave(&s_lock, flags);
        } 
        else
        {
            break;
        }
    }

    spin_unlock_irqrestore(&s_lock, flags);

    error = i;

    //printk("[PLOG]: Sent out %d/%ld bytes\n", i,(unsigned long)(len+i));

out:
    return error;
}

// We need to support this function if we want the select() call to behave
// properly on this file.
static unsigned int plog_poll(struct file *file, poll_table * wait)
{
    int           readable;
    unsigned long flags;

    poll_wait(file, &plog_wait, wait);

    spin_lock_irqsave(&s_lock, flags);
    
    if (s_plog_wp >= s_plog_rp) 
    {
        readable = s_plog_wp - s_plog_rp;
    } 
    else 
    {
        readable = s_plog_wp + (PLOG_LENGTH - s_plog_rp);
    }

    spin_unlock_irqrestore(&s_lock, flags);

    // If there is 4096 bytes, you can read data.
    if (readable >= 4096) 
    {
        return POLLIN | POLLRDNORM;
    }

    return 0;
}

static ssize_t plog_write(struct file *file, 
                          const char  *buf, 
                          size_t       len,
                          loff_t      *off)
{
    int error = -EPERM;
    char tempbuf[1024];
    const char *s;
    char *d;

    error = -EINVAL;
    if (!buf || len < 0 || len >= 1023) 
    {
        goto out;
    }

    error = 0;
    if (!len) 
    {
        goto out;
    }

    if (!access_ok(VERIFY_READ,buf,len))
    {
        goto out;
    }

    s = buf;
    d = tempbuf;
    error = len;
    while (len > 0) 
    {
        __get_user(*d, s);
        s++;
        d++;
        len--;
    }
    *d = 0;

    // Parse user's request
    // Request should be in form of "[+-]e1[-e2][,...]"
    s = tempbuf;
    while (*s != 0) 
    {
        int add;
        int start, end;
        int i;

        if (*s == '+') 
        {
            add = 1;
        } 
        else if (*s == '-') 
        {
            add = 0;
        } 
        else 
        {
            error = -EINVAL;
            break;
        }

        s++;

        start = 0;
        while (*s >= '0' && *s <= '9') 
        {
            start = start*10 + (*s - '0');
            s++;
        }

        if (*s == '-') 
        {
            s++;
            end = 0;
            while (*s >= '0' && *s <= '9') 
            {
                end = end*10 + (*s - '0');
                s++;
            }
        } 
        else 
        {
            end = start;
        }

        if (*s != 0 && *s != ',' && *s != '\n') 
        {
            error = -EINVAL;
            break;
        }

        if (*s == ',') 
        {
            s++;
        }

        if (*s == '\n') 
        {
            s++;
        }

        if (start < 0) 
        {
            start = 0;
        }

        if (end >= MAX_EVENT) 
        {
            end = MAX_EVENT - 1;
        }

        if (end >= start) 
        {
            for (i = start; i <= end; i++) 
            {
                if (add) 
                {
                    s_ptypeMask[i >> 5] &= ~(1 << (i & 0x1f));
                } 
                else 
                {
                    s_ptypeMask[i >> 5] |= (1 << (i & 0x1f));
                }
            }
        }
    }

out:
    return error;
}

static int plog_assign_region(void)
{
#ifndef USE_STATIC_PLOG
    unsigned long plog_region_start;
    unsigned long plog_size;
    unsigned long *pUlong;
    unsigned long ulongSize;
    unsigned long ulongIndex;

    // Only initialize once
    if (s_processLog != NULL) 
    {
        return 0;
    }

    if (mem_get_contigmem_region(PLOG_CONTIGMEM_REGION, 
                                 &plog_region_start, 
                                 &plog_size))
    {
        printk("[PLOG]: failed to assign contigmem region.\n");
        s_plog_disable = 1;
        return -EINVAL;
    }

    if (plog_size > 0) 
    {
        s_processLogSizeBytes = plog_size;
        s_processLog = (int *)plog_region_start;
    }

    printk("[PLOG]: Using contigmem %d, start = 0x%lx, size = %ld\n",
           PLOG_CONTIGMEM_REGION, 
           plog_region_start, 
           plog_size);

    pUlong = (unsigned long *)plog_region_start;
    ulongSize = plog_size / 4;
    for (ulongIndex = 0; ulongIndex < ulongSize; ulongIndex++)
    {
        *pUlong++ = ulongIndex;
    }
#endif
    return 0;
}

/* Write to the process log. */
static void _plLogEvent(int type, int dataLength, void *data)
{
    int *d, *s, *d_start;
    int           spaceNeeded;
    unsigned long flags;
    int           readable;
    int           skipLength;
    monotonic_t   now_time;
    int cpu;

    // Are we rejecting this type of event?
    if ( s_plog_disable != 0                  || 
         (type >= 0 && type < MAX_EVENT &&
         (s_ptypeMask[type >> 5] & (1 << (type & 0x1f))) != 0))
    {
        return;
    }

    /* Data can only be stored in 4 byte increments. */
    dataLength = (dataLength + 3) & ~0x3;

    if (dataLength > PLOG_MAX_DATASIZE) 
    {
        dataLength = PLOG_MAX_DATASIZE;
    }

    /* Allocate space. */
    spaceNeeded = PLOG_HEADER_SIZE + dataLength;

    /**
     * Making this code multi-thread safe.
     */

    spin_lock_irqsave(&s_lock, flags);

    tivo_monotonic_get_current(&now_time);

    if (s_plog_wp >= s_plog_rp) 
    {
        readable = s_plog_wp - s_plog_rp;
    } 
    else 
    {
        readable = s_plog_wp + (PLOG_LENGTH - s_plog_rp);
    }

    // Compute how much of the buffer we need to auto-consume to leave the
    // SAFETY_ZONE there.
    skipLength = PLOG_SAFETY_ZONE - (PLOG_LENGTH - readable);
    if (PLOG_LENGTH - s_plog_wp < spaceNeeded) 
    {
        skipLength += PLOG_LENGTH - s_plog_wp;
    }

    // Advance the read pointer to leave the safety zone large enough.
    while (skipLength > 0) 
    {
        int eventSize = s_processLog[s_plog_rp >> 2] & 0xffff;

        skipLength -= eventSize;
        s_plog_rp += eventSize;
        if (s_plog_rp == PLOG_LENGTH) 
        {
            s_plog_rp = 0;
        }
    }

    // Advance the write pointer if we cannot fit the event right here.
    if (PLOG_LENGTH - s_plog_wp < spaceNeeded) 
    {
        while (s_plog_wp != PLOG_LENGTH) 
        {
            s_processLog[s_plog_wp >> 2] = 4;
            s_plog_wp += 4;
        }
        s_plog_wp = 0;
    }

    /* Write log header */
    d_start = d = s_processLog + (s_plog_wp >> 2);

    s_plog_wp += spaceNeeded;

    /**
     * On 7450 smp_processor_id() returns 0 or 1 for SMP,
     * therefore no need to use cpu_number_map().
     */

    cpu = smp_processor_id();

    *d++ = (cpu << 31) | (type << 16) | spaceNeeded;
    *d++ = (int)tivo_monotonic_retrieve(&now_time);

    s = data;
    while (dataLength > 0) 
    {
        *d++ = *s++;
        dataLength -= 4;
    }

    spin_unlock_irqrestore(&s_lock, flags);

#if 0
    // Flush the event out to DRAM, so it's not stuck in the cache if
    // things just happen to lock up, in which case we might want to
    // look at the process log to find out why...
    dma_cache_wback_inv((unsigned long)d_start, (unsigned long)spaceNeeded);
#endif

    // Wake someone up?
    if (readable >= 4096 &&  type == PLOG_TYPE_SCE)
    {
        wake_up_interruptible(&plog_wait);
    }
}

// This is the exported module interface, there is less checking than user
// because the caller is assumed to know what he's doing...
//
int process_log(int type, int data_len, void *data)
{
    int buffer[PLOG_MAX_DATASIZE / sizeof(int)];

    buffer[0] = current->pid;
    if (data_len > PLOG_MAX_DATASIZE - 4) 
    {
        data_len = PLOG_MAX_DATASIZE - 4;
    }

    memcpy(buffer + 1, data, data_len);

    _plLogEvent(type, data_len + 4, buffer);
    return 0;
}

EXPORT_SYMBOL(process_log);

void _plog_swcpu(struct task_struct *from, struct task_struct *to)
{
    struct _pl_swe entry;

    entry.frompid = from->pid;
    entry.topid = to->pid;
    entry.rt_priority = to->rt_priority;
    entry.policy = to->policy;
    _plLogEvent(PLOG_TYPE_SWE, sizeof(entry), &entry);
}

void _plog_swcpu_end(int pid, int flags)
{
    int buffer[2];
    buffer[0] = pid;
    buffer[1] = flags;
    _plLogEvent(PLOG_TYPE_SWL, sizeof(buffer), buffer);
}

void _plog_rq_add(struct task_struct *tp) 
{
    struct _pl_rq entry;

    entry.pid = (short)tp->pid;
    entry.fromintr = in_interrupt();
    entry.priority = (short)tp->prio;
    entry.rt_priority = (short)tp->rt_priority;
    entry.policy = (short)tp->policy;
    entry.preempt = 0;                                // XXX
    _plLogEvent(PLOG_TYPE_RQ, sizeof(entry), &entry);
}

void _plog_rq_del(struct task_struct *tp) 
{
    struct _pl_drq entry;

    entry.pid = (short)tp->pid;
    entry.priority = (short)tp->prio;
    entry.rt_priority = (short)tp->rt_priority;
    entry.policy = (short)tp->policy;
    _plLogEvent(PLOG_TYPE_DRQ, sizeof(entry), &entry);
}

void _plog_setscheduler(int pid, int sched_pid, short priority, short policy) {
    int buffer[3];

    buffer[0]= pid;
    buffer[1]= sched_pid;
    buffer[2]= (priority << 16) | policy;
    _plLogEvent(PLOG_TYPE_SETSCHED, sizeof(buffer), buffer);
}

void _plog_intr_enter(int num, unsigned long pc)
{
    struct _pl_intre entry;

    entry.irq = num;
    entry.pc = pc;
    _plLogEvent(PLOG_TYPE_INTRE, sizeof(entry), &entry);
}

void _plog_intr_leave(int num)
{
    _plLogEvent(PLOG_TYPE_INTRL, sizeof(num), &num);
}

void _plog_bh_enter(int num)
{
    _plLogEvent(PLOG_TYPE_BHE, sizeof(num), &num);
}

void _plog_bh_leave(int num)
{
    _plLogEvent(PLOG_TYPE_BHL, sizeof(num), &num);
}

void _plog_filemap_page_swap(void *mm, int physPage, int virtPage)
{
    int buffer[3];

    buffer[0] = (int) mm;
    buffer[1] = physPage;
    buffer[2] = virtPage;
    _plLogEvent(PLOG_TYPE_FMS, sizeof(buffer), buffer);
}

void _plog_generic_page_swap(void *mm, int physPage, int virtPage)
{
    int buffer[3];

    buffer[0] = (int) mm;
    buffer[1] = physPage;
    buffer[2] = virtPage;
    _plLogEvent(PLOG_TYPE_GPS, sizeof(buffer), buffer);
}

void _plog_reswap(void *mm, int physPage, int virtPage)
{
    int buffer[3];

    buffer[0] = (int) mm;
    buffer[1] = physPage;
    buffer[2] = virtPage;
    _plLogEvent(PLOG_TYPE_RESWAP, sizeof(buffer), buffer);
}

void _plog_pageout(void *mm, int physPage, int virtPage)
{
    int buffer[3];

    buffer[0] = (int) mm;
    buffer[1] = physPage;
    buffer[2] = virtPage;
    _plLogEvent(PLOG_TYPE_PAGEOUT, sizeof(buffer), buffer);
}

void _plog_diskdispatch(unsigned long start, unsigned long sectors)
{
    int buffer[2];

    buffer[0] = (int)start;
    buffer[1] = (int)sectors;

    _plLogEvent(PLOG_TYPE_DISK_DISPATCH, sizeof(buffer), buffer);
}

#ifdef PLOG_26_XXX /**
                    * 'tivo' is not defined for 2.6
                    */

void _plog_diskstart(struct request *rq)
{
    int buffer[1];

    if (rq->tivo == NULL) 
    {
        buffer[0] = 0;      // type 0: non-TiVo request
    } 
    else if (tivo_monotonic_is_null(&rq->tivo->deadline)) 
    {
        buffer[0] = 2;      // type 2: TiVo non-RT request
    } 
    else 
    {
        buffer[0] = 1;      // type 1: TiVo RT request
    }

    _plLogEvent(PLOG_TYPE_DISK_START, sizeof(buffer), buffer);
}

void _plog_diskstop(struct request *rq)
{
    int buffer[3];
    struct idetivo_request *trq = rq->tivo;

    buffer[0] = rq->nr_sectors;
    if (trq != NULL && rq->nr_sectors > trq->max_nrsectors) 
    {
        buffer[0] = trq->max_nrsectors;
    }

    buffer[1] = rq->cmd == READ ? 0 : 1;

    if (trq == NULL || tivo_monotonic_is_null(&trq->deadline)) 
    {
        buffer[2] = 0;  // no deadline
    } 
    else 
    {
        monotonic_t now_time;
        s64 diff_count;

        tivo_monotonic_get_current(&now_time);

        diff_count = (s64)(tivo_monotonic_retrieve(&trq->deadline) -
                           tivo_monotonic_retrieve(&now_time));

        // clamp to min/max s32
        if (diff_count > (s64)0x000000007fffffffLL) 
        {
            buffer[2] = 0x7fffffff;
        } 
        else if (diff_count < (s64)(0xffffffff80000000LL)) 
        {
            buffer[2] = 0x80000000;
        } 
        else 
        {
            buffer[2] = (int)diff_count;
        }
    }

    _plLogEvent(PLOG_TYPE_DISK_STOP, sizeof(buffer), buffer);
}

#endif // PLOG_26_XXX

void _plog_generic_event0(int event)
{
    _plLogEvent(event, 0, NULL);
}

EXPORT_SYMBOL(_plog_generic_event0);

void _plog_generic_event(int event, int somedata)
{
    int buffer[2];
    if (current == NULL) 
    {
        buffer[0] = -1;
    } 
    else 
    {
        buffer[0] = current->pid;
    }
    buffer[1] = somedata;
    _plLogEvent(event, sizeof(buffer), buffer);
}

EXPORT_SYMBOL(_plog_generic_event);

void _plog_generic_event2(int event, int data1, int data2)
{
    int buffer[3];
    if (current == NULL) 
    {
        buffer[0] = -1;
    } 
    else 
    {
        buffer[0] = current->pid;
    }
    buffer[1] = data1;
    buffer[2] = data2;
    _plLogEvent(event, sizeof(buffer), buffer);
}

EXPORT_SYMBOL(_plog_generic_event2);

void _plog_generic_event3(int event, int data1, int data2, int data3)
{
    int buffer[4];
    if (current == NULL) 
    {
        buffer[0] = -1;
    } 
    else 
    {
        buffer[0] = current->pid;
    }
    buffer[1] = data1;
    buffer[2] = data2;
    buffer[3] = data3;
    _plLogEvent(event, sizeof(buffer), buffer);
}

EXPORT_SYMBOL(_plog_generic_event3);

void _plog_generic_event4(int event, int data1, int data2, int data3, int data4)
{
    int buffer[5];
    if (current == NULL) {
        buffer[0] = -1;
    } else {
        buffer[0] = current->pid;
    }
    buffer[1] = data1;
    buffer[2] = data2;
    buffer[3] = data3;
    buffer[4] = data4;
    _plLogEvent(event, sizeof(buffer), buffer);
}

EXPORT_SYMBOL(_plog_generic_event4);

void _plog_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
    struct _pl_ioctl entry;

    entry.pid = current->pid;
    entry.fd = fd;
    entry.cmd = cmd;
    entry.arg = arg;
    _plLogEvent(PLOG_TYPE_IOCTL, sizeof(entry), &entry);
}

#ifdef PLOG_26_XXX  /**
                     * 'rb_node_t' is not defined for 2.6 --> 
                     * plog_walk_vma_tree() is not build  -->
                     * plog_dump_vma() is not called.
                     */

static void plog_dump_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
    unsigned long vaddr;
    unsigned long vma_end;
    pgd_t *pgdir;
    int plogBuffer[4];

    plogBuffer[0] = (int) mm;

    // For each VMA...
    vaddr = vma->vm_start;
    vma_end = vma->vm_end;
    pgdir = pgd_offset(mm, vaddr);
    while (vaddr < vma_end) 
    {
        // Iterate over all the PGD's...
        unsigned long pgd_end;
        pmd_t *pmd;

        // On mips ... this function is just pmd = pgdir...
        // The pmd and pgd levels are the *same*.
        pmd = pmd_offset(pgdir, vaddr);
        pgd_end = (vaddr + PGDIR_SIZE) & PGDIR_MASK;
        if (pgd_end > vma_end) 
        {
            pgd_end = vma_end;
        }

        // Iterate over all the PMDs in this PGD (there is only ever one
        // since we are running with only two PTE levels)
        while (vaddr < pgd_end) 
        {
            unsigned long pmd_end;

            pmd_end = (vaddr + PMD_SIZE) & PMD_MASK;
            if (pmd_end > pgd_end) 
            {
                pmd_end = pgd_end;
            }

            if (pmd_none(*pmd)) 
            {
                vaddr = pmd_end;
            } 
            else 
            {
                pte_t *pte;

                pte = pte_offset(pmd, vaddr);

                // Iterate over the PTEs in the PMD.
                while (vaddr < pmd_end) 
                {
                    if (pte_present(*pte)) 
                    {
                        struct page *page = pte_page(*pte);
                        plogBuffer[1] = pte_val(*pte);
                        plogBuffer[2] = (int) vaddr;
                        plogBuffer[3] = page->flags;
                        _plLogEvent(PLOG_TYPE_PTE_PRESENT, sizeof(plogBuffer), 
                                plogBuffer);
                    }
                    vaddr += PAGE_SIZE;
                    pte++;
                }
            }
            pmd++;
        }
        pgdir++;
    }
}

static void plog_walk_vma_tree(struct mm_struct *mm, rb_node_t *pRbNode)
{
    if (pRbNode == NULL) return;
    plog_walk_vma_tree(mm, pRbNode->rb_left);
    plog_dump_vma(mm, rb_entry(pRbNode, struct vm_area_struct, vm_rb));
    plog_walk_vma_tree(mm, pRbNode->rb_right);
}

#endif // PLOG_26_XXX

static long plog_do_ioctl(struct file  *file,
                         unsigned int  cmd, 
                         unsigned long arg)
{
    long rc = -EINVAL;

    switch(cmd) {

    case PLOG_IOCTL_DUMP_MM:
    {
#ifdef PLOG_26_XXX
        int pid = arg;
        struct task_struct *p;

        rc = 0;
        read_lock(&tasklist_lock);

        p = find_task_by_pid(pid);
        if (p != NULL) 
        {
            struct mm_struct *mm = p->mm;
            if (mm != NULL) 
            {
                atomic_inc(&mm->mm_users);
                spin_lock(&mm->page_table_lock);
        
                plog_walk_vma_tree(mm, mm->mm_rb.rb_node);
        
                spin_unlock(&mm->page_table_lock);
                mmput(mm);
            }
        } 
        else 
        {
            rc = -ESRCH;
        }
        
        read_unlock(&tasklist_lock);
#else   
        printk("[PLOG]: PLOG_IOCTL_DUMP_MM ioctl is not ported to 2.6\n");
#endif      
    }
    break;

    case PLOG_IOCTL_PID_MMS:
    {
        struct task_struct *p;
        read_lock(&tasklist_lock);
        for_each_process(p) 
        {
            // Kernel tasks have no mm.  Don't log them.
            if (p->mm != NULL) 
            {
                int plogBuffer[2];
                plogBuffer[0] = p->pid;
                plogBuffer[1] = (int) p->mm;
                _plLogEvent(PLOG_TYPE_PID_MM, 
                            sizeof(plogBuffer), 
                            plogBuffer);
            }
        }
        read_unlock(&tasklist_lock);
        rc = 0;
    }
    break;

    case PLOG_LOG_EVENT:
    {
        struct descPlogEvent plogEventDesc;
        
        if (copy_from_user(&plogEventDesc, (void *)arg, sizeof(plogEventDesc)))
        {
            printk("[PLOG]: %s(): copy_from_user() failed?\n", 
                   __FUNCTION__);
        }
        else
        {
           
            rc = process_log(plogEventDesc.type, plogEventDesc.len, (void*)plogEventDesc.data);

            // debug debug
            // printk("[PLOG]: got PLOG_LOG_EVENT type = %d len = %d, data = %s\n", 
            //        plogEventDesc.type,
            //        plogEventDesc.len,
            //        plogEventDesc.data);
        }
    }
    break;

    case PLOG_LOG_GET_SIZE:
    {
        if (copy_to_user((void *)arg,
                         (void *)&s_processLogSizeBytes, 
                         sizeof(unsigned long)))
        {
            printk("[PLOG]: %s(): copy_to_user() failed?\n", 
                   __FUNCTION__);
        }
        else
        {
            rc = 0;
        }
    }
    break;

    default:              
        printk("[PLOG]: %s() got unexpeced cmd = %d\n", __FUNCTION__, cmd);
        break;
    }

    return rc;
}

/**
 * Support for /proc/plog interface.
 */
static struct file_operations proc_plog_operations = {
    read:    plog_read,
    write:   plog_write,
    poll:    plog_poll,
    unlocked_ioctl:   plog_do_ioctl,
    open:    plog_open,
    release: plog_release,
};

// We usually do a 1 second delay on panic, then reboot.  If ints are
// enabled at the time, that can fill up the process log with interrupt
// events, when we're probably more interested in what caused the panic...
//
static int plog_panic_event(struct notifier_block *self,
                            unsigned long          event,
                            void                  *ptr)
{
    s_plog_disable = 1;
    return NOTIFY_DONE;
}

/**
 * DO NOT USE THIS FUNCTION unless it's wednesday.
 * No, but seriously, it totally destroys plog, but it
 * gets events exactly as they happened at the insertion
 * point.  Which is what we need for panic logger, but
 * shouldn't be used anywhere else.
 */

void plog_destructive_liforead(int *buf, int entries)
{
    /* Figure out where we should start */
    s_plog_disable = 1;
    if (s_processLog != NULL) 
    {
        int plp=s_plog_wp - (entries<<2);
        if (plp < 0) 
        {
            plp+=PLOG_LENGTH;
        }
        s_plog_wp = plp;
        while(entries--) 
        {
            *buf++ = s_processLog[plp >> 2];
            plp += 4;
            if (plp >= PLOG_LENGTH) 
            {
                plp=0;
            }
        }
    }
}

static struct notifier_block plog_panic_block = {
    plog_panic_event,
    NULL,
    0
};

static int __init plog_init(void)
{
    struct proc_dir_entry *entry;

    // Assign the contigmem region for plog
    if (plog_assign_region()) 
    {
        return -ENOMEM;
    }

    s_plog_disable = 0;

    init_waitqueue_head(&plog_wait);

    entry = create_proc_entry("plog", S_IFREG | S_IRUSR | S_IWUSR, 0);
    if (entry) 
    {
        entry->nlink = 1;
        entry->proc_fops = &proc_plog_operations;

    }

    atomic_notifier_chain_register(&panic_notifier_list, &plog_panic_block);

    return 0;
}

/**
 * Assembly is passing 3 args to us in a0, a1, and a2.
 * We chose to log just two first ones.
 */
void plog_syscall_start(int syscall_num, long arg0, long arg1, long nargs)
{
    struct _pl_sce entry;
    int    entry_size = sizeof(entry);
    entry.pid = current->pid;
    entry.scnum = syscall_num;
    entry.arg0 = arg0;
    entry.arg1 = arg1;
    BUG_ON(nargs < 0);
    if (nargs < 2)
    {
        entry_size -= (sizeof(long) << (1 - nargs));
    }
    _plLogEvent(PLOG_TYPE_SCE, entry_size, &entry);
}

void plog_syscall_end(int syscall_num, int rv)
{
    struct _pl_scl entry;
    entry.pid = current->pid;
    entry.scnum = syscall_num;
    entry.rv = rv;
    _plLogEvent(PLOG_TYPE_SCL, sizeof(entry), &entry);
}

module_init(plog_init);
