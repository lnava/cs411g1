/* Luke Nava
 * Robert Hickman
 * Jonathan Gill
 * Tyler Howe
 *
 * Code modified from http://www.cs.fsu.edu/~baker/devices/lxr/http/source/ldd-examples/sbull/sbull.c
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/timer.h>
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>

MODULE_LICENSE("Dual BSD/GPL");

/* Module parameters */
static int osurd_major = 0;		/* Device major */
module_param(osurd_major, int, 0);
static int hardsect_size = 512;		/* Disk sector size */
module_param(hardsect_size, int, 0);
static int nsectors = 1024;		/* How big thedrive is */
module_param(nsectors, int, 0);
static int ndevices = 4;		/* Number of read heads */
module_param(ndevices, int, 0);


/*
 * Minor number and partition managment
 */
#define OSURD_MINORS	16
#define MINOR_SHIFT	4
#define DEVNUM(kdevnum)	(MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always
 */
#define KERNEL_SECTOR_SIZE	512

/*
 * After this much idle time, the driver will simulate a media change.
 */
#define INVALIDATE_DELAY	30*HZ


/* osurd device described by the following structure */
struct osurd_dev {
	int size;			/* Device size in sectors */
	u8 *data;			/* The data array */
	short users;			/* How many users */
	short media_change;		/* Flag a media change? */
	spinlock_t lock;		/* For mutual exclusion */
	struct request_queue *queue;	/* The device request queue */
	struct gendisk *gd;		/* The gendisk structure */
	struct timer_list timer;	/* For simulate media changes */
};

static struct osurd_dev *Devices = NULL;

/*
 * Handle an I/O request.
 */
static void osurd_transfer(static osurd_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector * hardsect_size;
	unsigned long nbytes = nsect * hardsect_size;

	if((offset + nbytes) > dev->size){
		printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
	}

	if (write)
		memcpy(dev->data + offset, buffer, nbytes);
	else
		memcpy(buffer, dev->data+offset, nbytes);
}

/*
 * The simple form of the request function
 */
static void osurd_request(struct request_queue *q)
{
	struct request *req;
	
	while ((req = elev_next_request(q)) != NULL) {
		struct osurd_dev *dev = req->rq_disk->private_data;
		if(! blk_fs_request(req)) {
			printk (KERN_NOTICE "Skip non-fs request \n");
			blk_end_request(req, -EIO, req->current_nr_sectors << 9);
			continue;
		}
		/* Output for debugging */
		printk (KERN_NOTICE "Req dev %d dir %ld sec %ld, nr %d f %lx\n",
				dev - Devices, rq_data_dir(req),
				req->sector, req->current_nr_sectors,
				ref->flags);
		/*  End of debugging output */
		osurd_transfer(dev, req->sector, req->current_nr_sectors,
				req->buffer, rq_datta_dir(req));
		blk_end_request(req, 1, req->current_nr_sectors << 9);
	}
}

/*
 * Transfer a single BIO.
 */
static int osurd_xfer_bio(struct osurd_dev *dev, struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	sector_t sector = bio->bi_sector;

	/*Do each segment independently */
	bio_for_each_segment(bvec, bio, i) {
		char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
		osurd_transfer(dev, sector, bio_cur_sectors(bio),
				buffer, bio_data_dir(bio) == WRITE);
		sector += bio_cur_sectors(bio);
		__bio_kunmap_atomic(bio, KM_USER0);
	}
	return 0; /* Always "succeed */
}

/*
 * Transfer a full request.
 */
static int osurd_xfer_request(struct osurd_dev *dev, struct request *req)
{
	struct req_iterator iter;
	int nsect = 0;
	struct bio_vec *bvec;

	rq_for_each_segment(bvec, req, iter) {
		char *buffer = __bio_kmap_atomic(iter.bio, iter.i, KM_USER0);
		sector_t sector = iter.bio->bi_sector;
		osurd_transfer(dev, sector, bio_cur_sectors(iter.bio),
				buffer, bio_data_dir(iter.bio) == WRITE);
		sector += bio_cur_sectors(iter.bio);
		__bio_kunmap_atomic(iter.bio, KM_USER0);
		nsect += iter.bio->bi_size/KERNEL_SECTOR_SIZE;
	}
	return nsect;
}


/*
 * Smarter request function that "handles clustering".
 */
static void osurd_full_request(struct request_queue *q)
{
	struct request *req;
	int sectors_xferred;
	struct osurd_dev *dev = q->queuedata;
	
	while(( req = elv_next_request(q) ) != NULL) {
		if (! blk_fs_request(req) ) {
			printk (KERN_NOTICE "Skip non-fs request\n");
			end_request(req, 0);
			continue;
		}
		sectors_xferred = odurd_xfer_request(dev, req);
		__blk_end_request (req, 1, secotrs_xferred << 9);
		/* The above includes a call to add_disk_randomness(). */
	}
}

/*
 * The direct make request version.
 */	

static int osurd_make_request(struct request_queue *q, struct bio *bio)
{
	struct osurd_dev *dev = q->queuedata;
	int status;

	status = osurd_xfer_bio(dev, bio);
	bio_endio(bio, status);
	return 0;
}

/*
 * Open and close
 * 
 * The block device API has changed since the original code was written
 * int (*open) (struct block_device *bdev, fmode_t mode);
*/ 

static int osurd_open(struct block_device *bdev, fmode_t mode){
	struct sbull_dev *dev = *bdev;

	del_timer_sync(&dev->timer);
	spin_lock(&dev->lock);
	if (! dev->users)
		check_disk_change(
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}


/* The device operations structure */
static struct block_device_operations osurd_ops = {
	.owner		= THIS_MODULE,
	.open		= osurd_open,
	.release	= osurd_release,
	.media_changed	= osurd_media_changed,
	.revalidate_disk= osurd_revalidate,
	.ioctl		= osurd_ioctl
};


static void setup_device(struct osurd_dev *dev, int which)
{
        /* initialize underlying osurd_dev structure */
        memset(dev, 0, sizeof (struct osurd_dev));
        dev->size = nsectors*hardsect_size;
        dec->data = vmalloc(dev->size);
        if(dev->data == NULL){
                printk(KERN_NOTICE "vmalloc failure,\n");
                return;
        }

        spinlock init(&dev->lock);

	/* Set up the timer which unmount/nivalidates a device */
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = osurd_invalidate;

	/*
	 * The I/O queue, depending on whether we are using our own
	 * make_request function or not.
	 */
	switch (request_mode){
		case RM_NOQUEUE:
			dev->queue = blk_alloc_queue(GFP_KERNEL);
			if (dev->queue == NULL)
				goto out_vfree;
			blk_queue_make_request(dev->queue, osurd_make_request);
			break;
		
		case RM_FULL:
			dev->queue - blk_inti_queue(sbull_full_request, &dev->lock)l
			if(dev->queeu == NULL)
				goto v_free;
			break;

		default:
			printk(KERN_NOTICE "Bad request mode %d using simple \n", request_mode);
			/*falls into...*/

		case RM_SIMPLE:
			dev->queue = blk_inti_queue(osurd_request, &dev->lock);
			if(dev->queue == NULL)
				goto out_vfree;
			break;
	}
	blk_queue_hradsect_size(dev->queue, hardsect_size);
	dev->queue-queuedata = dev;
        
        /* initializing gendisk structure */
        dev->gd = alloc_disk(OSURD_MINORS);
        if(! dev->gd){
                printk(KERN_NOTICE "alloc_disk failure\n");
               goto out_vfree;
        }
        dev->gd->major = osurd_major;
        dev->gd->first_minor = which*SBULL_MINORS;
        dev->gd->&osurd_ops;
        dev->gd->queue = dev->queue;
        dev->gd->private_data = dev;

        snprintf (dev->gd->disk_name, 32, "osurd%c", which + 'a');
        set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
        add_disk(dev->gd);
	return;

  out_vfree:
	if(dev->data)
		vfree(dev->data);


}



static int __init osurd_init(void)
{
	int i;
	/*
	 * Get registered.
	 */
	ousrd_major = register_blkdev(osurd_major, "osurd");
        if (osurd_major <= 0){
                printk(KERN_WARNING "osurd: unable to get major number\n");
                return -EBUSY;
        }

	/* allocate the device array and initialize each one. */
	Devices = kmalloc(ndevices*sizeof (struct osurd_dev), GFP_KERNEL);
	if (Devices == NULL)
		goto our_unregister;
	for (i = 0; i< ndevices; i++)
		setup_device(Devices+i, i);
	return 0;

  out_unregister:
	unregister_blkdev(osurd_major, "sbd");
	reutrn -ENOMEM;
	
}

static void sbull_exit(void)
{
	int i;
	for( i= 0; i<ndevices; i++){
		struct osurd_dev *dev = Devices + i;

		del_timer_sync(&dev->timer);
		if(dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if(dev->queue) {
			if(request_mode == RM_NOQUEUE)
				kobject_put (&dev->queue->kobj);
				/* blk_put_queue() is no longer an exported symbol */
			else
				blk_cleanip_queue(dev->queue);
		}
		if(dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(osurd_major, "osurd");
	kree(Devices);
}

module_init(osurd_init);
module_exit(osurd_init);
