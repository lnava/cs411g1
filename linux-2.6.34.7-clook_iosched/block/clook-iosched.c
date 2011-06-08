/*
 * elevator clook
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>


struct clook_data {
	struct list_head queue;
	struct list_head nextq;
	sector_t cur_pos;
};

static void clook_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int clook_dispatch(struct request_queue *q, int force)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (!list_empty(&cd->queue)) {
		struct request *rq;
		rq = list_entry(cd->queue.next, struct request, queuelist);
		cd->cur_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);
		list_del_init(&rq->queuelist);
		elv_dispatch_add_tail(q, rq);
		return 1;
	}else if(!list_empty(&cd->queue)){
		cd->queue = cd->nextq;
		INIT_LIST_HEAD(&cd->nextq);
		clook_dispatch(q, force);
	}
	return 0;
}

static void clook_add_request(struct request_queue *q, struct request *rq)
{	
	struct clook_data *cd = q->elevator->elevator_data;
	struct request *entry; /* used for iterating through lists */
	sector_t req_pos = blk_rq_pos(rq);/* Holds the position of the current request */
	sector_t cur_pos = cd->cur_pos; /* Where the head currently is */ 

	if(req_pos > cur_pos){
		if( clook_queue_empty(&cd->queue))
			list_add( &rq->queuelist, &cd->queue);
		else{
			list_for_each(entry, &cd->queue, queuelist){
				cur_pos = blk_re_pos(entry);
				if(cur_pos == NULL || cur_pos < req_pos)
					list_add( &(rq->queuelist), &(entry->queuelist.prev) );
			}
		}
	}else{
		if( clook_queue_empty( &cd->nextq) )
			list_add( &rq->queuelist, &cd->nextq );
		else{
			list_for_each(entry, &cd->queu, queuelist){
				cur_pos = blk_rq_pos(entry);
				if(cur_pos == NULL || cur_pos < req_pos)
					list_add( &(rq->queuelist), &(entry->queuelist.prev) );
			}	
		}

	//list_add_tail(&rq->queuelist, &cd->queue);
}

static int clook_queue_empty(struct request_queue *q)
{
	struct clook_data *cd = q->elevator->elevator_data;

	return list_empty(&cd->queue);
}

static struct request *
clook_former_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &cd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
clook_latter_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (rq->queuelist.next == &cd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static void *clook_init_queue(struct request_queue *q)
{
	struct clook_data *cd;

	cd = kmalloc_node(sizeof(*cd), GFP_KERNEL, q->node);
	if (!cd)
		return NULL;
	INIT_LIST_HEAD(&cd->queue);
	INIT_LIST_HEAD(&cd->nextq);
	cd->cur_pos = 0;
	return cd;
}

static void clook_exit_queue(struct elevator_queue *e)
{
	struct clook_data *cd = e->elevator_data;

	BUG_ON(!list_empty(&cd->queue));
	kfree(cd);
}

static struct elevator_type elevator_clook = {
	.ops = {
		.elevator_merge_req_fn		= clook_merged_requests,
		.elevator_dispatch_fn		= clook_dispatch,
		.elevator_add_req_fn		= clook_add_request,
		.elevator_queue_empty_fn	= clook_queue_empty,
		.elevator_former_req_fn		= clook_former_request,
		.elevator_latter_req_fn		= clook_latter_request,
		.elevator_init_fn		= clook_init_queue,
		.elevator_exit_fn		= clook_exit_queue,
	},
	.elevator_name = "clook",
	.elevator_owner = THIS_MODULE,
};

static int __init clook_init(void)
{
	elv_register(&elevator_clook);

	return 0;
}

static void __exit clook_exit(void)
{
	elv_unregister(&elevator_clook);
}

module_init(clook_init);
module_exit(clook_exit);


MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("No-op IO scheduler");
