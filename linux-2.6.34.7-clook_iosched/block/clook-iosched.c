/*
 * Luke Nava, Jonathan Gill
 * Robert Hickman, Tyler Howe
 *
 * elevator C_Look
 * Modified version of the noop-iosched.c so that the
 * I/O scheduler implements a C_Look algorithm.
 *
 *c_look: Scheduler that dispatches I/O requests
 *        in order of ascending sector number
 *
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

struct clook_data {
	struct list_head cur_queue;
        struct list_head next_queue;
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

	/* Dispatch from the current queue */
	if (!list_empty(&cd->cur_queue)) {
		struct request *rq;
		rq = list_entry(cd->cur_queue.next, struct request, queuelist);
		list_del_init(&rq->queuelist);
		cd->cur_pos = blk_rq_pos(rq);
		elv_dispatch_add_tail(q, rq);
		return 1;
	}else if(!list_empty(&cd->next_queue)){ /* If we have finished the current queue but not the next queue */
	        struct list_head temp_queue;
                temp_queue = cd->cur_queue;
		cd->cur_queue = cd->next_queue;
		cd->next_queue = temp_queue;
		clook_dispatch(*q, force);
	}
	
	/* Both queues are empty - return 0 */
	return 0;
}

static void clook_add_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;
	sector_t req_pos = blk_rq_pos(rq);
	
	/* Can the request be serviced on this pass?*/
	if(req_pos > cd->cur_pos){
	        
	        if( list_empty( &cd->cur_queue) )
		       list_add(rq, &cd->cur_queue.next)
		else{
	               list_for_each(entry, &cd->cur_queue){
	                     if( blk_rq_pos(entry->next ) > req_pos)
			             list_add(rq, &cd->cur_queue.next);
		       }
		}
	}else{ /* Need to service the request on the next pass over the drive. */
	        if( list_empty(&cd->next_queue) )
	               list_add(rq, &cd->next_queue.next);
	        else{
	               list_for_each(entry, &cd->next_queue){
	                       if( blk_rq_pos(entry->next) > req_pos )
	                              list_add(rq, &cd->next_queue.next);
		       }
		}
	}
}

static int clook_queue_empty(struct request_queue *q)
{
	struct clook_data *cd = q->elevator->elevator_data;

	return (list_empty(&cd->cur_queue) && list_empty(&cd->next_queue);
}

static struct request *
clook_former_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &cd->cur_queue)
		return NULL;


	/* If the request starts the next_queue, it's former must be the end of the cur_queue */
	if (rq->queuelist.prev == &cd->next_queue)
	        return list_entry(cd->cur_queue.prev, struct request, queuelist);
	
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
clook_latter_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (rq->queuelist.next == &cd->next_queue)
		return NULL;
	


	/* If the current request is at the end of the current queue,
	 * it's latter must be at the beggining of the next_queue */
	
	if (rq->queuelist.next == &cd->cur_queue)
	        return list_entry(cq->next_queue.next, struct request, queuelist);

	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static void *clook_init_queue(struct request_queue *q)
{
	struct clook_data *cd;

	cd = kmalloc_node(sizeof(*cd), GFP_KERNEL, q->node);
	if (!nd)
		return NULL;
	INIT_LIST_HEAD(&cd->cur_queue);
	INIT_LIST_HEAD(&cd->next_queue);
	cd->cur_pos = 0;
	return nd;
}

static void clook_exit_queue(struct elevator_queue *e)
{
	struct clook_data *cd = e->elevator_data;

	BUG_ON(!list_empty(&cd->cur_queue));
	BUG_ON(!list_empty(&cd->next_queue));
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


MODULE_AUTHOR("Luke Nava");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("clook IO scheduler");
