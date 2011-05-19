/*
 * elevator clook io scheduler
 * CS411 group 1:
 * Robert Hickman
 * Lucas Nava
 * Jonathan Gill
 * Tyler Howe
 *
 * Changes:
 * We changed the NOOP scheduler implementation of the FIFO algorithm
 * into an implementation of the CLOOK scheduling algorithm.
 * 
 * The functions we changed were:
 * clook_dispatch, clook_add_request, clook_init_queue
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

static sector_t cur_head_pos;

struct clook_data {
	struct list_head queue;
};

static void clook_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int clook_queue_empty(struct request_queue *q)
{
	struct clook_data *nd = q->elevator->elevator_data;

	return list_empty(&nd->queue);
}


static int clook_dispatch(struct request_queue *q, int force)
{
	struct clook_data *nd = q->elevator->elevator_data;
	
	if (!list_empty(&nd->queue)) {
		struct request *rq;
		rq = list_entry(nd->queue.next, struct request, queuelist);
		cur_head_pos = blk_rq_pos(rq);

		/* Print out [CLOOK] dsp <direction> <sector> */
		printk("[CLOOK] dsp <%c> <%llu>\n", rq_data_dir(rq) ? 'W' : 'R', (unsigned long long)cur_head_pos);
                
		/* Update disk head position. */
		cur_head_pos += blk_rq_sectors(rq);

		list_del_init(&rq->queuelist);
		elv_dispatch_add_tail(q, rq);
		return 1;
	}
	return 0;
}

static void clook_add_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *nd = q->elevator->elevator_data;
        struct request *entry;
        sector_t cur_pos = cur_head_pos;
	sector_t new_pos = blk_rq_pos(rq);
	sector_t first_pos;

	/* Print out [CLOOK] add <direction> <sector> */
	printk("[CLOOK] add <%c> <%llu>\n", rq_data_dir(rq) ? 'W' : 'R', (unsigned long long)new_pos);

	entry = list_entry(nd->queue.next, struct request, queuelist);
	first_pos = blk_rq_pos(entry);
	
	if( clook_queue_empty( q ) ) {
        	list_add( &rq->queuelist, &nd->queue );
	}
	else if( ( new_pos < cur_pos ) && ( cur_pos < first_pos ) ) {

		sector_t prev_loc;
                
		entry = list_entry(nd->queue.prev, struct request, queuelist);
		prev_loc = blk_rq_pos(entry);
		
		
		/* Check to see if this is first entry of next trip. */
		if( prev_loc >= first_pos ) {
                	list_add( &rq->queuelist, nd->queue.prev );
		} 
		else {
			/* New request cannot be serviced on this trip. */
			list_for_each_entry_reverse(entry, &nd->queue, queuelist) {
				
				sector_t entry_loc = blk_rq_pos(entry);
				
				prev_loc = blk_rq_pos(list_entry(entry->queuelist.prev, struct request, queuelist));

				if( entry_loc < new_pos ) {
					list_add( &rq->queuelist, &entry->queuelist );
					break;
				}
				else if( prev_loc > entry_loc ) {
                                	list_add( &rq->queuelist, &entry->queuelist.prev );
					break;
				}
			}
		}

	}
	else {
		struct request *next;
		sector_t next_pos;
		sector_t entry_loc;

		/* Check if less than first element in list. */
		if( new_pos < first_pos ) {
			list_add( &rq->queuelist, &nd->queue );
		}
		else {
			/* Request can be handled on this trip */
			list_for_each_entry(entry, &nd->queue, queuelist) {
				
				entry_loc = blk_rq_pos(entry);
				
				/* Current entry is end of list. */
				if( &(entry->queuelist.next) == &(nd->queue) ) {
					list_add( &(rq->queuelist), &(entry->queuelist) );
					break;
				}
				
				next = list_entry(entry->queuelist.next, struct request, queuelist);
				
				next_pos = blk_rq_pos(next);
				
				if( ( new_pos > entry_loc ) ) {
					/* Nominal case. */
					if ( next_pos > new_pos ) {
						list_add( &(rq->queuelist), &(entry->queuelist) );
						break;
					}
					/* Last request for this trip. */
					else if ( next_pos < entry_loc ) {
						list_add( &(rq->queuelist), &(entry->queuelist) );
						break;
					}
				}
				/* Keep iterating; new is greater than next. */
				else {
					continue; 
				}
			}
		}

	}
}

static struct request *
clook_former_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
clook_latter_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.next == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static void *clook_init_queue(struct request_queue *q)
{
	struct clook_data *nd;

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd)
		return NULL;
	INIT_LIST_HEAD(&nd->queue);
	cur_head_pos = 0;
	return nd;
}

static void clook_exit_queue(struct elevator_queue *e)
{
	struct clook_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
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


MODULE_AUTHOR("CS411g1");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CLook IO scheduler");
