/*;``
 * elevator clook io scheduler
 * CS411 group 1
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

sector_t cur_head_pos;

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
		cur_head_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);

		/* Print out [CLOOK] dsp <direction> <sector> */
		printk("[CLOOK] dsp <%c> <%lu>\n", rq_data_dir(rq) ? 'W' : 'R', (unsigned long)cur_head_pos);

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

	/* Print out [CLOOK] add <direction> <sector> */
	printk("[CLOOK] add <%c> <%lu>\n", rq_data_dir(rq) ? 'W' : 'R', new_pos);

	printk("New request pos: %lu, Current Pos: %lu \n", new_pos, cur_pos);
        
	if( clook_queue_empty( q ) ) {
        	list_add( &rq->queuelist, &nd->queue );
		printk("Empty List.  Adding current to list.\n");
	}
	else if( new_pos < cur_pos ) {

		sector_t last_loc;
                
		entry = list_entry(nd->queue.prev, struct request, queuelist);
		last_loc = blk_rq_pos(entry);
		
		/* Check to see if this is first entry of next trip. */
		if( last_loc > cur_pos ) {
                	list_add( &rq->queuelist, nd->queue.prev );
			printk("First Entry of Next Trip.\n");
		} 
		else {
			/* New request cannot be serviced on this trip. */
			list_for_each_entry_reverse(entry, &nd->queue, queuelist) {
				
				sector_t entry_pos = blk_rq_pos(entry);

				if( entry_pos < new_pos ) {
					list_add( &rq->queuelist, &entry->queuelist );
					printk("Adding after %lu and before %lu (NXT).\n", entry_pos, blk_rq_pos( list_entry( entry->queuelist.next, struct request, queuelist ) ) );
					break;
				}
			}
		}

	}
	else {
		/* Request can be handled on this trip */
		list_for_each_entry(entry, &nd->queue, queuelist) {
		     	struct request *next;
			sector_t next_pos;
			sector_t entry_pos;
			
			entry_pos = blk_rq_pos(entry);
			
			/* Current entry is end of list. */
			if( &(entry->queuelist.next) == &(nd->queue) ) {
				list_add( &(rq->queuelist), &(entry->queuelist) );
				printk("Adding after %lu (EOL).\n", entry_pos );
                                break;
			}
			
			next = list_entry(entry->queuelist.next, struct request, queuelist);
			
			next_pos = blk_rq_pos(next);
                	
			if( ( new_pos > entry_pos ) ) {
				/* Nominal case. */
				if ( next_pos > new_pos ) {
					list_add( &(rq->queuelist), &(entry->queuelist) );
					printk("Adding after %lu and before %lu. \n", entry_pos, next_pos );
				        break;
				}
				/* Last request for this trip. */
				else if ( next_pos < entry_pos ) {
					list_add( &(rq->queuelist), &(entry->queuelist) );
					printk("Adding after %lu and before %lu (EOT).\n", entry_pos, next_pos );
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
	cur_head_pos = 10;
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
