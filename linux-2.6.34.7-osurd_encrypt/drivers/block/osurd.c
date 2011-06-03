/* 
 * CS411 Group 1
 * Luke Nava
 * Robert Hickman
 * Jonathan Gill
 * Tyler Howe
 *
 * OSURD: Oregon State University Ramdisk Module.
 * A simulated block device driver that creates a bio-aware ramdisk module
 * for use in the linux kernel.  The code from the website has been modified
 * to allow for command-line configuration of the crypto key, the retrieval
 * of the geometry, and the simulated ejection of the disk (this part is extra
 * credit).  The read and write operations have also been encrypted using the
 * AES crypto algorithm in the crypto API.
 *
 * Code modified from http://hi.baidu.com/casualfish/blog/item/873696dd599ed9d48d10299e.html
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>

#include <linux/crypto.h>
#include <crypto/aes.h>
#include <linux/stat.h>
#include <linux/cdrom.h> /* for the cdrom eject code */

MODULE_LICENSE("Dual BSD/GPL");
/* function definitions */
static void osurd_exit(void);
static void osurd_encrypt(char *data, int len, int enc);

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
 * The different "request modes" we can use.
 */
enum {
	RM_SIMPLE = 0, /* The extra-simple request function */
	RM_FULL = 1,   /* The full-blown version */
	RM_NOQUEUE = 2, /* Use make_request */
};

static int request_mode = RM_SIMPLE;
module_param(request_mode, int, 0);

/*Variables used for encryption*/
static struct crypto_cipher *tfm;
static int key_len = 8;
static char crypto_key[8] = "\xdf\xa6\xbf\x4d\xed\x81\xdb\x61";
module_param_array(crypto_key, byte, &key_len, 0444);

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
};

static struct osurd_dev *Devices = NULL;

/*
 * Handle an I/O request.
 */
static void osurd_transfer(struct osurd_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector*KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;

	if((offset + nbytes) > dev->size){
		printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
	}

	if (write){
		osurd_encrypt(&buffer, nbytes, write);
		memcpy(dev->data + offset, buffer, nbytes);
	}
	else{
		memcpy(buffer, dev->data+offset, nbytes);
		osurd_encrypt(&buffer, nbytes, write);
	}
}

static void osurd_encrypt(char *data, int len, int enc)
{
        int k;


	if(enc){
		for(k=0; k<len; k+=crypto_cipher_blocksize(tfm)){
			crypto_cipher_encrypt_one(tfm, data+k, data+k);
		}
		return;
	}else{
		for(k=0; k<len; k+=crypto_cipher_blocksize(tfm)){
			crypto_cipher_decrypt_one(tfm, data+k, data+k);
		}
	}
}

/*
 * The simple form of the request function
 */
static void osurd_request(struct request_queue *q)
{
	struct request *req;
      //  char *data;
	//int req_size;

	req = blk_fetch_request(q);
	
	while (req != NULL) {
		struct osurd_dev *dev = req->rq_disk->private_data;
		if(! blk_fs_request(req)) {
			printk (KERN_NOTICE "Skip non-fs request \n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
	

		//req_size = blk_rq_cur_sectors(req)*KERNEL_SECTOR_SIZE;
                //data = kmalloc(req_size, GFP_KERNEL);

		/* Debuggin printk statements */
		printk(KERN_NOTICE "Req dev: %d\ndir: %llu\n sector: %llu\n nr: %d\n HZ:%d\n-------\n",
					 dev-Devices, rq_data_dir(req), (long long unsigned)blk_rq_pos(req), 
						blk_rq_sectors(req), HZ);
		/* End of debugging output */

		if(rq_data_dir(req)){
			//memcpy(data, req->buffer, req_size);
                	
			/*osurd_encrypt(data, req_size, 1); *//* encrypt */ 

			osurd_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),
				req->buffer, rq_data_dir(req));
	        } else {
			osurd_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),
				req->buffer, rq_data_dir(req));
                        
			/*osurd_encrypt(data, req_size, 0); *//* decrypt */
			
			//memcpy(req->buffer, data, req_size);
		}

		if( !__blk_end_request_cur(req, 0))
			req = blk_fetch_request(q);

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

	/* Do each segment independently. */
	bio_for_each_segment(bvec, bio, i) {
		char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
		osurd_transfer(dev, sector, bio_cur_bytes(bio)>>9,
				buffer, bio_data_dir(bio) == WRITE);
		sector += bio_cur_bytes(bio)>>9;
		__bio_kunmap_atomic(bio, KM_USER0);
	}
	return 0; /* Always "succeed" */
}

/*
 * Transfer a full request.
 */
static int osurd_xfer_request(struct osurd_dev *dev, struct request *req)
{
	struct bio *bio;
	int nsect = 0;
	
	__rq_for_each_bio(bio, req) {
		osurd_xfer_bio(dev, bio);
		nsect += bio->bi_size/KERNEL_SECTOR_SIZE;
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
	
	while( (req = blk_fetch_request(q)) != NULL ) {
		if (! blk_fs_request(req) ) {
			printk (KERN_NOTICE "Skip non-fs request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		sectors_xferred = osurd_xfer_request(dev, req);
		if (!__blk_end_request_cur(req, 0))
			blk_fetch_request(q);
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
 * Open
 * 
 * The block device API has changed since the original code was written
 * int (*open) (struct block_device *bdev, fmode_t mode);
*/ 
static int osurd_open(struct block_device *bdev, fmode_t mode)
{
	struct osurd_dev *dev = bdev->bd_disk->private_data;
	
	spin_lock(&dev->lock);
	if (! dev->users)
		check_disk_change(bdev);
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}

/*
 * Close the media
 */
static int osurd_release(struct gendisk *disk, fmode_t mode)
{
	struct osurd_dev *dev = disk->private_data;

	spin_lock(&dev->lock);
	dev->users--;
	spin_unlock(&dev->lock);

	return 0;
}

/*
 * Look for a (simulated) media change
 */
int osurd_media_changed(struct gendisk *gd)
{
	struct osurd_dev *dev = gd->private_data;

	return dev->media_change;
}

/*
* Revalidate. WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
* with open. That needs to be reevaluated.
*/
int osurd_revalidate(struct gendisk *gd)
{
	struct osurd_dev *dev = gd->private_data;

	if (dev->media_change) {
		dev->media_change = 0;
		memset (dev->data, 0, dev->size);
	}
	return 0;
}

/* 
 * Code has been removed from this function to allow getgeo at 
 * the module parameter level 
 */
int osurd_ioctl (struct block_device *device, fmode_t mode,
unsigned int cmd, unsigned long arg)
{
        struct osurd_dev *dev = device->bd_disk->private_data;
	unsigned int ret_code = 0;
        
	switch(cmd) {
		case CDROMEJECT:
			spin_lock(&dev->lock);
			if( dev->users > 0 ) {
				printk(KERN_WARNING "osurd: Eject failed. Users > 0\n");
				ret_code = -EBUSY;
			}
			else {
				dev->media_change = 1;
			}
			spin_unlock(&dev->lock);
			return ret_code;
	 }

        return -ENOTTY; /* Unknown Command */
}

/*
 * Get geometry: since we are a virtual device, we have to make
 * up something plausible. So we claim 16 sectors, four heads,
 * and calculate the corresponding number of cylinders. We set the
 * start of data at sector four.
 */
static int osurd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	long size; 
        struct osurd_dev *dev = bdev->bd_disk->private_data;

	size = dev->size * (hardsect_size/KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 4;
	return 0;
}

/* The device operations structure */
static struct block_device_operations osurd_ops = {
	.owner		= THIS_MODULE,
	.open		= osurd_open,
	.release	= osurd_release,
	.media_changed	= osurd_media_changed,
	.revalidate_disk= osurd_revalidate,
	.ioctl		= osurd_ioctl,
	.getgeo		= osurd_getgeo
};

/*
 * Setup the device
 */
static void setup_device(struct osurd_dev *dev, int which)
{
        /* initialize underlying osurd_dev structure */
        memset(dev, 0, sizeof (struct osurd_dev));
        dev->size = nsectors*hardsect_size;
        dev->data = vmalloc(dev->size);
        if(dev->data == NULL){
                printk(KERN_NOTICE "vmalloc failure,\n");
                return;
        }

        spin_lock_init(&dev->lock);

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
			dev->queue = blk_init_queue(osurd_full_request, &dev->lock);
			if(dev->queue == NULL)
				goto out_vfree;
			break;

		default:
			printk(KERN_NOTICE "Bad request mode %d using simple \n", request_mode);
			/*falls into...*/

		case RM_SIMPLE:
			dev->queue = blk_init_queue(osurd_request, &dev->lock);
			if(dev->queue == NULL)
				goto out_vfree;
			break;
	}
	blk_queue_logical_block_size(dev->queue, hardsect_size);
	dev->queue->queuedata = dev;
        
        /* initializing gendisk structure */
        dev->gd = alloc_disk(OSURD_MINORS);
        if(! dev->gd){
                printk(KERN_NOTICE "alloc_disk failure\n");
               goto out_vfree;
        }
        dev->gd->major = osurd_major;
        dev->gd->first_minor = which*OSURD_MINORS;
        dev->gd->fops = &osurd_ops;
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

/*
 * Init
 * Register the device and check module params.
 */
static int __init osurd_init(void)
{
	int i;
	
	/* Initialize Crypto info.
	if(sizeof(crypto_key)/sizeof(char) < key_len){
		printk( KERN_WARNING "key is too short for encryption\n");
		osurd_exit();
	}
	*/

	tfm = crypto_alloc_cipher("aes", 0, 0);
	crypto_cipher_setkey(tfm, crypto_key, key_len);

	/*
	 * Get registered.
	 */
	osurd_major = register_blkdev(osurd_major, "osurd");
        if (osurd_major <= 0){
                printk(KERN_WARNING "osurd: unable to get major number\n");
                return -EBUSY;
        }

	/* allocate the device array and initialize each one. */
	Devices = kmalloc(ndevices*sizeof (struct osurd_dev), GFP_KERNEL);
	if (Devices == NULL)
		goto out_unregister;
	for (i = 0; i< ndevices; i++)
		setup_device(Devices+i, i);
	return 0;

  out_unregister:
	unregister_blkdev(osurd_major, "sbd");
	return -ENOMEM;
	
}

/*
 * Exit:
 * Closed the module and frees up resources
 */
static void osurd_exit(void)
{
	int i;
	/*crypto_free_cipher(tfm);*/
	for( i= 0; i<ndevices; i++){
		struct osurd_dev *dev = Devices + i;

		if(dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if(dev->queue) {
			if(request_mode == RM_NOQUEUE)
				kobject_put (&dev->queue->kobj);
				/* blk_put_queue() is no longer an exported symbol */
			else
				blk_cleanup_queue(dev->queue);
		}
		if(dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(osurd_major, "osurd");
	kfree(Devices);

}

module_init(osurd_init);
module_exit(osurd_exit);
