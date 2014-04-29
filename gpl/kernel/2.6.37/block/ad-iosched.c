/*
 *  Anticipatory Deadline i/o scheduler.
 *  Derived from Deadline i/o scheduler.
 *  Proactively checks if any I/O request is going to miss the deadline.
 *  If so serves that request immediately out of order.
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/tivo-monotime.h>
#include <linux/ide-tivo.h>

#undef DEBUG_AD_SCHED
#ifdef DEBUG_AD_SCHED
#define DPRINTK(fmt, args...)           printk(fmt, ## args);      
#else
#define DPRINTK(fmt, args...)
#endif

// XXX: see comments in ad_move_request
//#define RESTORE_ELV_RQ_ORDER    1
#define RESTORE_ELV_RQ_ORDER    0

struct ad_data {
	/*
	 * run time data
	 */

	/*
	 * requests are present on both sort_list and fifo_list
	 */
	struct rb_root sort_list;	
	struct list_head fifo_list;

	/*
	 * next in (sector) sort order. 
	 */
	struct request *next_rq;
	sector_t last_sector;		/* head position */

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	int front_merges;
        int seek; /* Disk Avg seek time in msec */
        int rotation;/* Disk Avg Rotation time in msec */
        int transfer; /* Worst DTR in msec */
};

static void ad_move_request(struct ad_data *, struct request *);

/* Deadline estimation helper functions */
static inline unsigned long ad_estimate( struct ad_data *dd,
                    /*unsigned long current_sector,*/
                    struct request *req) 
{
        unsigned long result;

        result = dd->seek + /* initial seek time */
                dd->rotation + /* initial rotation time */ 
                (dd->transfer * blk_rq_sectors(req)) + /* transfer time */
                ((blk_rq_sectors(req)>>8) * dd->rotation); /* extra rotations */

        return result;
}

static inline unsigned long ad_estimate_noseek( struct ad_data *dd,
                       /*unsigned long current_sector,*/
                       struct request *req) 
{
        unsigned long result;

        /* Same, but don't include seek time (because it will be included
         in a subsequent time estimate)*/
        result = dd->rotation + /* initial rotation time */
                (dd->transfer *  blk_rq_sectors(req)) + /* transfer time */
                ((blk_rq_sectors(req)>>8) * dd->rotation); /* extra rotations */

        return result;
}

static inline struct rb_root *
ad_rb_root(struct ad_data *dd)
{
	return &dd->sort_list;
}

/*
 * get the request after `rq' in sector-sorted order
 */
static inline struct request *
ad_latter_request(struct request *rq)
{
	struct rb_node *node = rb_next(&rq->rb_node);

	if (node)
		return rb_entry_rq(node);

	return NULL;
}

/*
 * get the first rq in sector-sorted order
 */
static inline struct request *
ad_first_request(struct ad_data *dd)
{
	struct rb_node *node = rb_first(ad_rb_root(dd));

	if (node)
		return rb_entry_rq(node);

	return NULL;
}

static void
ad_add_rq_rb(struct ad_data *dd, struct request *rq)
{
	struct rb_root *root = ad_rb_root(dd);
	struct request *__alias;

	while (unlikely(__alias = elv_rb_add(root, rq)))
		ad_move_request(dd, __alias);
}

static inline void
ad_del_rq_rb(struct ad_data *dd, struct request *rq)
{
	if (dd->next_rq == rq)
		dd->next_rq = ad_latter_request(rq);

	elv_rb_del(ad_rb_root(dd), rq);
}

/*
 * add rq to rbtree and fifo
 */
static void
ad_add_request(struct request_queue *q, struct request *rq)
{
	struct ad_data *dd = q->elevator->elevator_data;
        struct list_head *list;

	ad_add_rq_rb(dd, rq);

        /* all VFS requests and few MFS requests with infinite deadline */
        if ( rq->deadline == INFINITE_DEADLINE ){
		list_add_tail(&rq->queuelist, &dd->fifo_list);
        }
        else if ( !rq->deadline ){  /* MFS requests with deadline value = 0 */
                rq->deadline = INFINITE_DEADLINE;
		list_add_tail(&rq->queuelist, &dd->fifo_list);
        }
        else{ /* media requests with valid deadline value */
                list_for_each(list, &dd->fifo_list) {
                        struct request *trq;
                        trq = list_entry(list, struct request, queuelist);
                        if (rq->deadline < trq->deadline)
                                break;
                }
                __list_add(&rq->queuelist, list->prev, list);
        }
}

/*
 * remove rq from rbtree and fifo
 */
static void ad_remove_request(struct request_queue *q, struct request *rq)
{
	struct ad_data *dd = q->elevator->elevator_data;

    	rq_fifo_clear(rq);
	ad_del_rq_rb(dd, rq);
}

static int
ad_merge(struct request_queue *q, struct request **req, struct bio *bio)
{
	struct ad_data *dd = q->elevator->elevator_data;
	struct request *__rq;
	int ret;

	/*
	 * check for front merge
	 */
	if (dd->front_merges) {
		sector_t sector = bio->bi_sector + bio_sectors(bio);

		__rq = elv_rb_find(&dd->sort_list, sector);
		if (__rq) {
			BUG_ON(sector != blk_rq_pos(__rq));

			if (elv_rq_merge_ok(__rq, bio)) {
				ret = ELEVATOR_FRONT_MERGE;
				goto out;
			}
		}
	}

	return ELEVATOR_NO_MERGE;
out:
	*req = __rq;
	return ret;
}

static void ad_merged_request(struct request_queue *q,
			      struct request *req, int type)
{
	struct ad_data *dd = q->elevator->elevator_data;

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (type == ELEVATOR_FRONT_MERGE) {
		elv_rb_del(ad_rb_root(dd), req);
		ad_add_rq_rb(dd, req);
	}
}

static void
ad_merged_requests(struct request_queue *q, struct request *req,
			 struct request *next)
{
        monotonic_t nts,ts;

	/*
	 * if next expires before rq, assign its expire time to rq
	 * and move into next position (next will be deleted) in fifo
	 */
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
                tivo_monotonic_store(&ts, req->deadline);
                tivo_monotonic_store(&nts, next->deadline);
		if (tivo_monotonic_lt(&nts,&ts)) {
			list_move(&req->queuelist, &next->queuelist);
			req->deadline = next->deadline;
		}
	}

	/*
	 * kill knowledge of next, this one is a goner
	 */
	ad_remove_request(q, next);
}

/*
 * move request from sort list to dispatch queue.
 */
static inline void
ad_move_to_dispatch(struct ad_data *dd, struct request *rq)
{
	struct request_queue *q = rq->q;

	ad_remove_request(q, rq);
	elv_dispatch_add_tail(q, rq);
}

/*
 * move an entry to dispatch queue
 */
static void
ad_move_request(struct ad_data *dd, struct request *rq)
{
#ifdef RESTORE_ELV_RQ_ORDER
// XXX: this seems awfully convoluted. why not just don't touch dd->next_rq if we don't
// want to disturb the next elevator order. For that matter, why would we _not_ want to
// restart the elevator at the (out-of-order) deadlined IO we're running?
        sector_t old_elv_pos = 0;

        /*
         * if the deadlined request is same as current elevator order
         * request, skip below old_elv_pos calculation etc ..
         */
	if ((dd->next_rq != NULL) && (dd->next_rq != rq)) { 
                /* 
                 * save elevator rb_key(sector offset) of the next request 
                 * in elevator order before jumping to deadlined request.
                 */
		if ((dd->next_rq)->cmd_type == REQ_TYPE_FS)
			old_elv_pos = blk_rq_pos(dd->next_rq);

                DPRINTK("ele rq: old_elv_pos = 0x%lx , sector = 0x%lx, nr_sectors = 0x%lx,"
                                " deadline = 0x%llx\n", old_elv_pos, blk_rq_pos(dd->next_rq),
                                blk_rq_sectors(dd->next_rq), dd->next_rq->deadline);

                DPRINTK("deadlined rq: sector = 0x%lx, nr_sectors = 0x%lx,"
                                " deadline = 0x%llx\n", blk_rq_pos(rq), 
                                blk_rq_sectors(rq), rq->deadline);
        }
#endif

	dd->next_rq = ad_latter_request(rq);

	dd->last_sector = rq_end_sector(rq);

	/*
	 * take it off the sort and fifo list, move
	 * to dispatch queue
	 */

        DPRINTK("Dispatching:: sector = 0x%lx, nr_sectors = 0x%lx, deadline = 0x%llx\n\n", 
                        blk_rq_pos(rq), blk_rq_sectors(rq), rq->deadline);

	ad_move_to_dispatch(dd, rq);

#ifdef RESTORE_ELV_RQ_ORDER
        if(old_elv_pos){
                /*
                 * restore the elevator where it was before 
                 * jumping to out-of-order deadlined request.
                 */
		dd->next_rq = elv_rb_find(&dd->sort_list, old_elv_pos);
        }
#endif        
}

/*
 * ad_check_fifo returns 0 if there are no expirable request on the fifo,
 * 1 otherwise. Requires !list_empty(&dd->fifo_list)
 */
static inline int ad_check_fifo(struct ad_data *dd)
{
	struct request *rq = rq_entry_fifo(dd->fifo_list.next);
        monotonic_t nts,ts;

        /*
         * avoid extra fudge in estimating the expiry condition for 
         * db requests with infinite deadline. 
         */
	if (rq->deadline == INFINITE_DEADLINE) {
                return 0;
        }

        tivo_monotonic_get_current(&nts);
	tivo_monotonic_add_usecs(&nts, ad_estimate(dd, rq));

	tivo_monotonic_store(&ts, rq->deadline);

	/*
	 * rq might expire before completion
	 */
	if (tivo_monotonic_lt(&ts,&nts))
		return 1;

	return 0;
}

/*
 * ad_dispatch_requests selects the best request according to
 * read/write expire etc
 */
static int ad_dispatch_requests(struct request_queue *q, int force)
{
	struct ad_data *dd = q->elevator->elevator_data;
	struct request *rq;

	if (list_empty(&dd->fifo_list)) {
	    return 0;
	}

        BUG_ON(RB_EMPTY_ROOT(&dd->sort_list));
    
	/*
	 * find best request 
	 */
	if (ad_check_fifo(dd)) {
		/* An expirable request exists - satisfy it */
		rq = rq_entry_fifo(dd->fifo_list.next);
	} else if (dd->next_rq) {
		/*
		 * We have a next request in sort order. No expirable requests
		 * so continue on from here.
		 */
		rq = dd->next_rq;
	} else {
		/*
		 * We have run out of higher-sectored requests. Go back to the
		 * lowest sectored request (1 way elevator) and start a new
		 * request.
		 */
		rq = ad_first_request(dd);
	}

	/*
	 * rq is the selected appropriate request.
	 */
	ad_move_request(dd, rq);

	return 1;
}

static int ad_queue_empty(struct request_queue *q)
{
	struct ad_data *dd = q->elevator->elevator_data;

	return list_empty(&dd->fifo_list);
}

static void ad_exit_queue(struct elevator_queue *e)
{
	struct ad_data *dd = e->elevator_data;

	BUG_ON(!list_empty(&dd->fifo_list));

	kfree(dd);
}

/*
 * initialize elevator private data (ad_data).
 */
static void *ad_init_queue(struct request_queue *q)
{
	struct ad_data *dd;

	dd = kmalloc_node(sizeof(*dd), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!dd)
		return NULL;

	INIT_LIST_HEAD(&dd->fifo_list);
	dd->sort_list = RB_ROOT;
	dd->front_merges = 1;

        /* Worst case seek + rotation and data transfer times from actual data.*/
        dd->seek = 19000; /* 19ms worst case seek (no rotation) */
        dd->rotation = 11111; /* 5400rpm = 90rps = 11111us/rev */
        dd->transfer = (20000/256); /* worst case DTR -- 20ms for 256 sectors*/

	return dd;
}

/*
 * sysfs parts below
 */

static ssize_t
ad_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
ad_var_store(int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtol(p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR)				        \
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct ad_data *dd = e->elevator_data;			        \
	int __data = __VAR;						\
	return ad_var_show(__data, (page));			        \
}
SHOW_FUNCTION(ad_front_merges_show, dd->front_merges);
SHOW_FUNCTION(ad_seek_show, dd->seek);
SHOW_FUNCTION(ad_rotation_show, dd->rotation);
SHOW_FUNCTION(ad_transfer_show, dd->transfer);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR)			                \
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct ad_data *dd = e->elevator_data;			        \
	int __data;							\
	int ret = ad_var_store(&__data, (page), count);		        \
        *(__PTR) = __data;					        \
	return ret;							\
}
STORE_FUNCTION(ad_front_merges_store, &dd->front_merges);
STORE_FUNCTION(ad_seek_store, &dd->seek);
STORE_FUNCTION(ad_rotation_store, &dd->rotation);
STORE_FUNCTION(ad_transfer_store, &dd->transfer);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, ad_##name##_show, \
				      ad_##name##_store)

static struct elv_fs_entry ad_attrs[] = {
	DD_ATTR(front_merges),
	DD_ATTR(seek),
	DD_ATTR(rotation),
	DD_ATTR(transfer),
	__ATTR_NULL
};

static struct elevator_type iosched_deadline = {
	.ops = {
		.elevator_merge_fn = 		ad_merge,
		.elevator_merged_fn =		ad_merged_request,
		.elevator_merge_req_fn =	ad_merged_requests,
		.elevator_dispatch_fn =		ad_dispatch_requests,
		.elevator_add_req_fn =		ad_add_request,
		.elevator_queue_empty_fn =	ad_queue_empty,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_init_fn =		ad_init_queue,
		.elevator_exit_fn =		ad_exit_queue,
	},

	.elevator_attrs = ad_attrs,
	.elevator_name = "ad",
	.elevator_owner = THIS_MODULE,
};

static int __init ad_init(void)
{
	elv_register(&iosched_deadline);

	return 0;
}

static void __exit ad_exit(void)
{
	elv_unregister(&iosched_deadline);
}

module_init(ad_init);
module_exit(ad_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("anticipatory deadline IO scheduler");
