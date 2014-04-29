/*
 * Copyright (C) 2006, 2007 TiVo Inc. All Rights Reserved.
 */ 

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/fastbufs.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/pgtable-bits.h>


# define dprintk(args...)
//# define dprintk(args...) printk(args)

extern int remap_pages(unsigned long address, phys_t phys_addr,
	                            phys_t size, unsigned long flags);

struct fbuf_zone{
#define FBUF_MAX_ZBMP_BYTES (FBUF_MAX_ENTS/8)
    unsigned char bitmap[FBUF_MAX_ZBMP_BYTES]; 
    int in_use;
    unsigned long  zone_start,zone_end;
};

static struct fbuf_zone *fbuf_zones;

struct fbuf_ent_sz_t {
    int sz;
    int limit;
};

/*Make an entry here for a new zone/type*/
static struct fbuf_ent_sz_t fbuf_ent_sz[FBUF_MAX_TYPES]= { 
                    {THREAD_FBUF_SZ, FBUF_MAX_THREADS},
                    {CNTXT_FBUF_SZ,FBUF_MAX_CNTXTS},
};
int fastbuf_initialized = 0;
extern unsigned long tivo_counts_per_256usec;

static ssize_t read_proc_fbuf(struct file * file, const char * buf,
                                size_t count, loff_t *ppos){

	if (*ppos >= sizeof(int))
		return 0;
	if (count > sizeof(int) - *ppos)
		count = sizeof(int) - *ppos;
	copy_to_user(buf, (unsigned int)&fastbuf_initialized + *ppos, count);
	*ppos += count;
	return count;
}

/*proc fs entry to read the init status of the fastbufs*/
static int read_fbuf_status (char *user_buf, char ** start, off_t offset,
                int size, int * eof, void * data)
{
    int count = 0;
	count += sprintf(user_buf,"%d\n", fastbuf_initialized);
    *eof = 1;
	return count;
}

static void do_fbuf_free(void *ptr){
    int c,i,zone;
    
    if (!ptr)   /*Do nothing*/
        return;

    /*Find the zone in which the ptr belongs to*/
    for ( zone = 0; zone < FBUF_MAX_TYPES; zone++){
        if ( (unsigned long)ptr >= fbuf_zones[zone].zone_start && 
                (unsigned long)ptr < fbuf_zones[zone].zone_end )
            break;
    }

    if ( fbuf_zones[zone].in_use <= 0)
        return;

    if ( zone >= FBUF_MAX_TYPES ) /*Do nothing*/
        return;

    switch(zone){
        case FBUF_THREAD_TYPE:
          current->exec_cntxt = NULL;
          break;
        case FBUF_CNTXT_TYPE:
          break;
        default:
          return;
    }

    c = ((unsigned long) ptr - 
            fbuf_zones[zone].zone_start)/fbuf_ent_sz[zone].sz;
    if ( c < 0 )
        return ;
    
    i = c/8;

    if (fbuf_zones[zone].bitmap[i] & (1<<(c%8))){
            fbuf_zones[zone].bitmap[i] &= ~(1<<(c%8));
            fbuf_zones[zone].in_use--;

            dprintk("Fbuf Free: in_use %d bitmap (%d:%d):%08x\n",
                    fbuf_zones[zone].in_use,i,c%8,
                    fbuf_zones[zone].bitmap[i]);
            return;
    }
    else {
        printk(KERN_ERR "Failed to allocate fastbuffer (in use %d)\n",fbuf_zones[zone].in_use);
    }
}

static void *do_fbuf_alloc(fbuf_type_t fbuf_zone){
    int c,i;
    void *ptr=NULL;
    thread_fbuf *t;
    context_fbuf *cntxt;

    if ( fbuf_zone > FBUF_MAX_TYPES )
        return NULL;

    if ( fbuf_zones[fbuf_zone].in_use >= fbuf_ent_sz[fbuf_zone].limit )
        return NULL;
        
    for ( c = 0; c < fbuf_ent_sz[fbuf_zone].limit/8; c++){
        for ( i = 0; i < 8 ; i++){

            if (!(fbuf_zones[fbuf_zone].bitmap[c] & (1<<i))){
                ptr = (void *)(fbuf_zones[fbuf_zone].zone_start + 
                        (((c*8)+i) * fbuf_ent_sz[fbuf_zone].sz));

                fbuf_zones[fbuf_zone].bitmap[c] |= (1<<i);
                fbuf_zones[fbuf_zone].in_use++;

                dprintk("(%d:%d)ptr %08x fbuf_zone %d in_use %d bitmap %08x "
                    "start %08x end %08x\n",
                    i,c,ptr,fbuf_zone, 
                    fbuf_zones[fbuf_zone].in_use,
                    fbuf_zones[fbuf_zone].bitmap[c],
                    fbuf_zones[fbuf_zone].zone_start, 
                    fbuf_zones[fbuf_zone].zone_end);
                
               goto found; 
            }
        }
    }
    return NULL;

found:

    switch ( fbuf_zone ){

        case FBUF_THREAD_TYPE:
          t = (thread_fbuf *) ptr;
          memset(t, 0, THREAD_FBUF_SZ);
          t->pid = current->pid;
          current->exec_cntxt = (void *)t;
        break;
          
        case FBUF_CNTXT_TYPE:
          cntxt = (context_fbuf *)ptr;
          memset(cntxt, 0, CNTXT_FBUF_SZ);
        break;

        default:
          ptr = NULL;
        break;
    }
    return ptr;
}

static int do_list_cntxts(const char *ustr, int all){
    int c,i;
    context_fbuf *cntxt;
    int len=0;
    char *str;
    int err,ret = 0;
    int count = 0;

    str = (char *) kmalloc( LIST_CNTXTS_REC_SZ * FBUF_MAX_CNTXTS, GFP_KERNEL );
    if ( !str ) 
        return -EINVAL;
    memset(str, 0, LIST_CNTXTS_REC_SZ * FBUF_MAX_CNTXTS);

    len += sprintf(str,"S.No\tAID\t\tPRI\tTIME(msec)\t\tNAME (PID,THREAD)\n");
    for ( c = 0; c < fbuf_ent_sz[FBUF_CNTXT_TYPE].limit/8 ; c++){
        for ( i = 0; i < 8 ; i++){
            if (fbuf_zones[FBUF_CNTXT_TYPE].bitmap[c] & (1<<i)){
                cntxt = (void *)(fbuf_zones[FBUF_CNTXT_TYPE].zone_start + 
                        (((c*8)+i) * fbuf_ent_sz[FBUF_CNTXT_TYPE].sz));
                
                if ( (len + LIST_CNTXTS_REC_SZ) >= (LIST_CNTXTS_REC_SZ * FBUF_MAX_CNTXTS)){
                    goto end;
                }

                if ( cntxt->thread_info){
                    len += sprintf(str+len,"%d\t%d\t%d\t%ld\t\t%s (%d,%s)\n", 
                            count++, 
                            cntxt->id, 
                            cntxt->priority,
                            cntxt->active_msecs,
                            cntxt->name, 
                            cntxt->thread_info->pid,cntxt->thread_info->name);
                }
            }
        }
    }
    
    if (!all)
        goto end;

    for ( c = 0; c < fbuf_ent_sz[FBUF_CNTXT_TYPE].limit/8 ; c++){
        for ( i = 0; i < 8 ; i++){
            if (fbuf_zones[FBUF_CNTXT_TYPE].bitmap[c] & (1<<i)){
                cntxt = (void *)(fbuf_zones[FBUF_CNTXT_TYPE].zone_start + 
                        (((c*8)+i) * fbuf_ent_sz[FBUF_CNTXT_TYPE].sz));
                
                if ( (len + LIST_CNTXTS_REC_SZ) >= (LIST_CNTXTS_REC_SZ * FBUF_MAX_CNTXTS) ){
                    goto end;
                }
                
                if ( !cntxt->thread_info ){
                    len += sprintf(str+len,"%d\t%d\t%d\t%ld\t\t%s\n",count++,
                            cntxt->id, cntxt->priority, cntxt->active_msecs, cntxt->name);
                }
                            
            }
        }
    }

end:    
    err = copy_to_user((void *)ustr,(void *)str, LIST_CNTXTS_REC_SZ * FBUF_MAX_CNTXTS);
    if ( err )
        ret = -EINVAL;

    kfree(str);
    return ret;
}

#define FBUF_MAJOR 242
int fbuf_open (struct inode *inode, struct file *filp){
    return 0;
}

int fbuf_ioctl (struct inode *inode, struct file *file, 
            unsigned int cmd, unsigned long arg) {
    int err;
    struct fbuf_req fbuf_req;
    

    if (!fastbuf_initialized){
        //printk(KERN_ERR "FastBuffers are not initialized\n");
        return -EINVAL;
    }

    if(_IOC_TYPE(cmd) != FBUF_IOC_MAGIC) {
        printk(KERN_ERR "[fbuf_ioctl] cmd (0x%x) mismatch, "
                        "called with 0x%x while expecting 0x%x\n", 
                        cmd, _IOC_TYPE(cmd), FBUF_IOC_MAGIC);
        return -EINVAL;
    }

    switch(cmd){
        case FBUF_ALLOC:
            err = copy_from_user((void *)&fbuf_req,
                                (void *)arg,
                                sizeof(struct fbuf_req));
            if ( err )
                return -EFAULT;
            fbuf_req.ptr = do_fbuf_alloc(fbuf_req.fbuf_type);
            err = copy_to_user((void *)arg,
                                (void *)&fbuf_req,
                                sizeof(struct fbuf_req));
            if ( err )
                return -EFAULT;
        break;

        case FBUF_FREE:
            do_fbuf_free((void *)arg);
        break;

        case FBUF_LIST_CNTXTS:
            return do_list_cntxts((char *)arg,0);
        break;

        case FBUF_LIST_ALL_CNTXTS:
            return do_list_cntxts((char *)arg,1);
        break;

        default:
            printk(KERN_ERR "fbuf_ioctl: Invalid command\n");
            return -EINVAL;
    }
    return 0;
}

static struct file_operations fbuf_proc_ops= {
	read: read_proc_fbuf,
};

static struct file_operations fbuf_fops = {
    ioctl:   fbuf_ioctl,
    open:    fbuf_open,
};

static __init void init_fbufs(void){
    struct page *pgs; 
    struct page *page; 
    unsigned long lo0, lo1, hi;
    int fbuf_pgs;
    int i,tlb_order;
    int pg_order;
    int fbuf_sz;
    struct proc_dir_entry *ent;

#if !defined(CONFIG_TIVO_DEVEL) && \
        (defined(CONFIG_TIVO_UMA2C) || defined(CONFIG_TIVO_NGP0))
    /* Donot enable Fastbufs in production kernels of TCDs < Series3
     for the matter of not having enough physical memory*/
    return;
#endif
    i = register_chrdev(FBUF_MAJOR, "fbuf", &fbuf_fops);
    if ( i < 0 ){
        printk(KERN_ERR "Failed to register char dev [major %d]\n",FBUF_MAJOR);
        return;
    }

    if ((ent = create_proc_entry("fbuf", S_IRUGO, NULL)) != NULL) {
        ent->read_proc = read_fbuf_status;
        ent->size = sizeof(int);
        ent->proc_fops = &fbuf_proc_ops;
    }
    else{
        printk("Unable to do create_proc_entry for PC_samples\n");
        return;
    }

    for ( fbuf_sz = 0, i = 0; i < FBUF_MAX_TYPES; i++){
        fbuf_sz += fbuf_ent_sz[i].limit * fbuf_ent_sz[i].sz; 
    }

    fbuf_sz += sizeof(fastbuf_hdr);
    fbuf_sz -= (fbuf_sz &  (PAGE_SIZE-1)) + 
                    (fbuf_sz & (PAGE_SIZE-1)) ? PAGE_SIZE:0;
    
    if ( fbuf_sz <= 4*1024 ){
            tlb_order = PM_4K;
            fbuf_sz = 4 * 1024;
    }
    else if ( fbuf_sz > 4*1024 && fbuf_sz <= 16*1024 ){
            tlb_order = PM_16K;
            fbuf_sz = 16 * 1024;
    }
    else if ( fbuf_sz > 16*1024 && fbuf_sz <= 64*1024 ){
            tlb_order = PM_64K;
            fbuf_sz = 64 * 1024;
    }
    else{
        printk("%d KB of fastbuf memory is requested\n",fbuf_sz/1024);
        printk("Maximum allocatable Fbuf memory is 1024KB\n");
        printk("Bump up the limits in kernel and recompile it\n");
        return;
    }
    dprintk("Fbuf_chunk %ldKB, tlb_order %d\n",fbuf_sz/1024, tlb_order);
    
    fbuf_pgs = fbuf_sz / PAGE_SIZE;
    pg_order = get_order(fbuf_sz);

    pgs = (struct page *) alloc_pages(GFP_KERNEL,pg_order);
    memset(page_address(pgs),0,fbuf_sz);

    /* Lock the pages and avoid swapping*/
    for (page = pgs, i = 0; i < fbuf_pgs; i++, page++){
        if (TryLockPage(page))
            goto bail_out;
    }
    
    /*Fastbufs can be accessed in interrupt context as well. 
     So add TLB wired entry. Tell OS that we are reserving the address range.
     This is a dummy page table entry. This never gets refilled in the 
     TLB as we already have an entry with the same VM address*/
    if (remap_pages(FBUF_MAGIC_VMSTART, __pa(page_address(pgs)), fbuf_sz, 0)){
        return;
    }
    
    /* Make a wired TLB*/
    hi = FBUF_MAGIC_VMSTART;
    lo0 = ((__pa(page_address(pgs))>>6)&0x3fffffc0)|0x7;
    lo1 = 0x1;

    add_wired_entry(lo0,lo1,hi,tlb_order);

    /*zone management stuff allocation & initialization*/
    fbuf_zones = kmalloc(FBUF_MAX_TYPES * sizeof(struct fbuf_zone), 
                            GFP_KERNEL );
    if (!fbuf_zones)
        goto bail_out;

    /*Initialize fastbuf_hdr*/
    fastbuf->version = FBUF_VERSION_1;
    fastbuf->cur_thread_info = NULL;
    memset(&fastbuf->t,0,sizeof(monotonic_t));
    fastbuf->mono_counts_per_msec = (tivo_counts_per_256usec / 256)*1000;

    /*Initializing zones*/
    fbuf_zones[0].zone_start = FBUF_MAGIC_VMSTART + sizeof(fastbuf_hdr);
    fbuf_zones[0].zone_end = fbuf_zones[0].zone_start + 
                                fbuf_ent_sz[0].limit* fbuf_ent_sz[0].sz;

    fbuf_zones[0].in_use = 0;
    memset(fbuf_zones[0].bitmap,0,FBUF_MAX_ZBMP_BYTES);

    for ( i = 1; i < FBUF_MAX_TYPES; i++){
        fbuf_zones[i].zone_start = fbuf_zones[i-1].zone_end;
        fbuf_zones[i].zone_end = fbuf_zones[i].zone_start + 
                                    fbuf_ent_sz[i].limit* fbuf_ent_sz[i].sz;
        fbuf_zones[i].in_use = 0;
        memset(fbuf_zones[i].bitmap,0,FBUF_MAX_ZBMP_BYTES);
    }
    
    fastbuf_initialized = ENABLE_FASTBUFS;

    return;

bail_out: 
    free_pages(page_address(pgs), pg_order);
}

module_init(init_fbufs);
