/*
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>
#include <linux/dm-io.h>

#define DM_MSG_PREFIX "foolcache"

 /* TODOs:
merge the bitmaps



 */


struct foolcache_c {
	struct dm_dev* cache;
	struct dm_dev* origin;
	struct dm_io_client* io_client;
	unsigned int bypassing;
	unsigned long sectors, last_caching_sector;
	unsigned int block_size;		// block (chunk) size, in sector
	unsigned int block_shift;
	unsigned int block_mask;
	unsigned long* bitmap;
	unsigned long* copying;
	unsigned int bitmap_sectors;
	struct completion copied;
//	struct dm_kcopyd_client* kcopyd_client;
//	struct job_kcopyd* queue;
//	spinlock_t qlock;
};
/*
struct job_kcopyd {
	struct job_kcopyd* next;
	struct bio* bio;
	struct foolcache_c* fcc;
	unsigned int bi_size;
	unsigned long current_block, end_blocks;
	struct dm_io_region origin, cache;
};
*/
const static char SIGNATURE[]="FOOLCACHE";
struct header {
	char signature[sizeof(SIGNATURE)];
	unsigned int block_size;
};

static inline unsigned long sector2block(struct foolcache_c* fcc, sector_t sector)
{
	return sector >> fcc->block_shift;
}

static inline sector_t block2sector(struct foolcache_c* fcc, unsigned long block)
{
	return block << fcc->block_shift;
}

static int write_ender(struct foolcache_c* fcc)
{
	int r;
	char buf[512];
	struct header* header = (struct header*)buf;
	struct dm_io_region region = {
		.bdev = fcc->cache->bdev,
		.sector = fcc->sectors - (fcc->bitmap_sectors + 1),
		.count = fcc->bitmap_sectors,
	};
	struct dm_io_request io_req = {
		.bi_rw = WRITE,
		.mem.type = DM_IO_VMA,
		.mem.ptr.vma = fcc->bitmap,
		// .notify.fn = ,
		// .notify.context = ,
		.client = fcc->io_client,
	};
	r=dm_io(&io_req, 1, &region, NULL);
	if (r!=0) return r;

	memcpy(header->signature, SIGNATURE, sizeof(SIGNATURE));
	header->block_size = fcc->block_size;
	region.sector = fcc->sectors - 1;
	region.count = 1;
	io_req.mem.ptr.vma = buf;
	r = dm_io(&io_req, 1, &region, NULL);
	return r;
}

static int read_ender(struct foolcache_c* fcc)
{
	int r;
	char buf[512];
	struct header* header = (struct header*)buf;
	struct dm_io_region region = {
		.bdev = fcc->cache->bdev,
		.sector = fcc->sectors - 1,
		.count = 1,
	};
	struct dm_io_request io_req = {
		.bi_rw = READ,
		.mem.type = DM_IO_VMA,
		.mem.ptr.vma = buf,
		// .notify.fn = ,
		// .notify.context = ,
		.client = fcc->io_client,
	};
	r=dm_io(&io_req, 1, &region, NULL);
	if (r!=0) return r;

	r=strncmp(header->signature, SIGNATURE, sizeof(SIGNATURE)-1);
	if (r!=0) return r;
	if (header->block_size != fcc->block_size) return -EINVAL;

	io_req.mem.ptr.vma = fcc->bitmap;
	region.sector = fcc->last_caching_sector + 1;
	region.count = fcc->bitmap_sectors;
	r = dm_io(&io_req, 1, &region, NULL);
	return r;
}

/*
static void job_bio_callback_done(unsigned long error, void *context)
{
	struct bio* bio = context;
	bio_endio(bio, error);
}

static void job_bio_callback_further(unsigned long error, void *context)
{
	struct bio* bio = context;
	if (unlikely(error))
	{
		bio_endio(bio, error);
	}

	bio->bi_bdev = fcc->origin->bdev;
	bio->bi_sector += bio->bi_size;
	bio->bi_size = job->bi_size - bio->bi_size;
	do_bio(fcc, bio, job_bio_callback_done, NULL);
}

static void do_bio(struct foolcache_c* fcc, struct bio* bio, io_notify_fn callback, void* ctx)
{
	struct dm_io_region region = {
		.bdev = bio->bdev,
		.sector = bio->bi_sector,
		.count = bio_sectors(bio),
	};
	struct dm_io_request io_req = {
		.bi_rw = READ,
		.mem.type = DM_IO_BVEC,
		.mem.ptr.bvec = bio->bi_io_vec + bio->bi_idx,
		.notify.fn = callback,
		.notify.context = ctx,
		.client = fcc->io_client,
	};
	BUG_ON(dm_io(&io_req, 1, &region, NULL));
}

void split_bio(struct bio* bio)
{
	unsigned int bi_size = bio->bi_size;
	bio->bi_bdev = fcc->cache->bdev;
	bio->bi_size = (fcc->last_caching_sector - bio->bi_sector) * 512;
	do_bio(fcc, bio, job_bio_callback_further, (void*)bi_size);
}

static void copy_block_async(struct job_kcopyd* job);
void kcopyd_do_callback(int read_err, unsigned long write_err, void *context)
{
	bool no_intersection;
	struct job_kcopyd* job = context;
	struct foolcache_c* fcc = job->fcc;
	unsigned long block = job->current_block;

	if (unlikely(read_err))
	{
		bio_endio(bio, -EIO);
		mempool_free(job, fcc->job_pool);
		return;
	}

	if (unlikely(write_err))
	{
		fcc->bypassing = 1;
		bio->bi_bdev = fcc->origin->bdev;
		do_bio(fcc, bio, do_jobbio_callback_done);
		mempool_free(job, fcc->job_pool);
		return;
	}

	set_bit(block, fcc_.bitmap);
	block = find_next_missing_block(fcc, ++block);
	if (block!=-1)
	{
		job->current_block = block;
		copy_block_async(job);
		return;
	}

	// copy done, read from cache
	no_intersection = (job->end_block <= fcc->last_caching_block);
	mempool_free(job, fcc->job_pool);
	if (likely(no_intersection))
	{	// the I/O region doesn't involve the ender, do it as a whole
		bio->bi_bdev = fcc->cache->bdev;
		do_bio(fcc,bio, do_jobbio_callback_done);
	}
	else do_bio_split(bio);	// the I/O region involves the ender, do it seperately
}

struct job_kcopyd* queued_job_for_block(struct foolcache_c* fcc, unsigned long block)
{
	struct job_kcopyd* next;
	struct job_kcopyd* q = NULL;
	struct job_kcopyd* prev = NULL;
	struct job_kcopyd* node = fcc->queue;
	while (node)
	{
		next = node->next;
		if (block == node->current_block)
		{	// remove it from old queue, and insert it into new queue
			if (prev) prev->next = node->next;
			else fcc->queue = node->next;
			node->next = q;
			q = node;
		}
		prev = node;
		node = next;
	}
	return q;
}

void copy_block_callback(int read_err, unsigned long write_err, void *context)
{
	struct job_kcopyd* qjob;
	struct job_kcopyd* job = context;
	struct foolcache_c* fcc = job->fcc;
	unsigned long block = job->current_block;

	spin_lock_irq(fcc->qlock);
	clear_bit(block, fcc->copying);
	qjob = queued_job_for_block(fcc, block);
	spin_unlock_irq(fcc->qlock);

	while (qjob)
	{
		kcopyd_do_callback(read_err, write_err, qjob);
		qjob = qjob->next;
	}
	kcopyd_do_callback(read_err, write_err, job);
}

static void copy_block_async(struct job_kcopyd* job)
{
	struct foolcache_c* fcc = job->fcc;
	job->origin.sector = job->cache.sector = 
		(job->current_block << fcc->block_shift);

	spin_lock_irq(fcc->qlock);
	if (test_bit(job->current_block, fcc->copying))
	{	// the block is being copied by another thread, let's wait in queue
		job->next = fcc->queue;
		fcc->queue = job;
		spin_unlock_irq(fcc->qlock);
		return;
	}
	set_bit(job->current_block, fcc->copying);
	spin_unlock_irq(fcc->qlock);


// int dm_kcopyd_copy(struct dm_kcopyd_client *kc, struct dm_io_region *from,
// 		   unsigned num_dests, struct dm_io_region *dests,
// 		   unsigned flags, dm_kcopyd_notify_fn fn, void *context);
	dm_kcopyd_copy(fcc->kcopyd_client, &job->origin, 1, &job->cache, 
		0, kcopyd_callback, job);
}

unsigned long find_next_missing_block(struct foolcache_c* fcc, 
	unsigned long start, unsigned long end)
{
	if (end > fcc->last_caching_block) 
	{
		end = fcc->last_caching_block;
	}
	for (; start<=end; ++start)
	{
		if (!test_bit(start, fcc->bitmap))
		{
			return start;
		}
	}
	return -1;
}

static void fc_map(struct foolcache_c* fcc, struct bio* bio)
{
	unsigned long start_block = sector2block(bio->bi_sector);
	unsigned long end_block = sector2block(bio->bi_sector + bio->bi_size/512);
	unsigned long i = find_next_missing_block(fcc, start, end);

	if (i!=-1)
	{	// found a missing block
		struct job* = mempool_alloc(fcc->job_pool, GFP_NOIO);
		job->fcc = fcc;
		job->bio = bio;
		job->current_block = i;
		job->end_block = end_block;
		job->origin->bdev = fcc->origin->bdev;
		job->cache->bdev = fcc->cache->bdev;
		job->origin.count = fcc->block_size;
		job->cache.count = fcc->block_size;
		kcopyd(job);
		return DM_MAPIO_SUBMITTED;
	}

	if (end_block > fcc->last_caching_block)
	{
		do_bio_split(bio);
		return DM_MAPIO_SUBMITTED;
	}

	bio->bi_bdev = fcc->cache->bdev;
	return DM_MAPIO_REMAPPED;
}
*/
static int copy_block(struct foolcache_c* fcc, unsigned int block)
{
	int r = 0;
	char buf[fcc->block_size];
	struct dm_io_region region;
	struct dm_io_request io_req;

	// before copying
	if (fcc->bypassing || test_bit(block, fcc->bitmap)) return 0;
	if (test_and_set_bit(block, fcc->copying))
	{	// the block is being copied by another thread, let's just wait
retry:
		wait_for_completion_timeout(&fcc->copied, 1*HZ);
		if (fcc->bypassing)
		{
			return -EIO;
		}
		if (test_bit(block, fcc->copying))
		{
			goto retry;
		}
		return 0;
	}
	if (test_bit(block, fcc->bitmap)) goto out;

	// do reading
	region.bdev = fcc->origin->bdev,
	region.sector = (block << fcc->block_shift),
	region.count = fcc->block_size,
	io_req.bi_rw = READ,
	io_req.mem.type = DM_IO_VMA,
	io_req.mem.ptr.vma = buf,
	// io_req.notify.fn = ,
	// io_req.notify.context = ,
	io_req.client = fcc->io_client,
	r=dm_io(&io_req, 1, &region, NULL);
	if (r!=0) goto out;

	// do writing
	io_req.bi_rw = WRITE;
	region.bdev = fcc->cache->bdev;
	r=dm_io(&io_req, 1, &region, NULL);
	if (r!=0)
	{
		r = 0;	// do NOT report write errors
		fcc->bypassing = 1;	// and bypass cache
		goto out;
	}
	set_bit(block, fcc->bitmap);

out:// after copying
	clear_bit(block, fcc->copying);
	complete_all(&fcc->copied);
	return r;
}

static int foolcache_map_sync(struct foolcache_c* fcc, struct bio* bio)
{
	sector_t last_sector;
	if (bio->bi_rw==WRITE)
	{
		bio_endio(bio, -EROFS);
		return DM_MAPIO_SUBMITTED;
	}

	last_sector = bio->bi_sector + bio->bi_size/512 - 1;
	if (fcc->bypassing || last_sector > fcc->last_caching_sector)
	{
		bio->bi_bdev = fcc->origin->bdev;
	}
	else
	{	// preparing the cache, followed by remapping
		unsigned long end_block = sector2block(fcc, last_sector);
		unsigned long i = sector2block(fcc, bio->bi_sector);
		for (; i<=end_block; ++i)
		{
			if (!test_bit(i, fcc->bitmap))
			{
				copy_block(fcc, i);
			}		
		}
		bio->bi_bdev = fcc->bypassing ? fcc->origin->bdev : fcc->cache->bdev;
	}
	return DM_MAPIO_REMAPPED;
}

static inline bool isorder2(unsigned int x)
{
	return (x & (x-1)) == 0;
}

/*
 * Construct a foolcache mapping
 *      origin cache block_size [create]
 */
static int foolcache_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct foolcache_c *fcc;
	unsigned int bs, bitmap_size, r;

	if (argc<2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	fcc = kzalloc(sizeof(*fcc), GFP_KERNEL);
	if (fcc == NULL) {
		ti->error = "dm-foolcache: Cannot allocate foolcache context";
		return -ENOMEM;
	}

	if (dm_get_device(ti, argv[0], FMODE_READ, &fcc->origin)) {
		ti->error = "dm-foolcache: Device lookup failed";
		goto bad1;
	}

	if (dm_get_device(ti, argv[1], FMODE_READ|FMODE_WRITE, &fcc->cache)) {
		ti->error = "dm-foolcache: Device lookup failed";
		goto bad2;
	}

	fcc->sectors = (i_size_read(fcc->origin->bdev->bd_inode) >> SECTOR_SHIFT);
	if (fcc->sectors != (i_size_read(fcc->cache->bdev->bd_inode) >> SECTOR_SHIFT))
	{
		ti->error = "dm-foolcache: Device sub-device size mismatch";
		goto bad3;
	}

	if (sscanf(argv[2], "%u", &bs)!=1 || bs<4 || bs>1024*1024 || !isorder2(bs)) {
		ti->error = "dm-foolcache: Invalid block size";
		goto bad3;
	}
	bs*=(1024/512); // KB to sector
	fcc->block_size = bs;
	fcc->block_shift = ffs(bs);
	fcc->block_mask = ~(bs-1);

	fcc->bitmap_sectors = fcc->sectors/bs/8/512 + 1; 	// sizeof bitmap, in sector
	fcc->last_caching_sector = fcc->sectors - 1 - 1 - fcc->bitmap_sectors;
	bitmap_size = fcc->bitmap_sectors*512;
	fcc->bitmap = kmalloc(bitmap_size, GFP_KERNEL);
	fcc->copying = kmalloc(bitmap_size, GFP_KERNEL);
	if (fcc->bitmap==NULL || fcc->copying==NULL)
	{
		ti->error = "dm-foolcache: Cannot allocate bitmaps";
		goto bad4;
	}

	fcc->io_client = dm_io_client_create();
	if (IS_ERR(fcc->io_client)) 
	{
		ti->error = "dm-foolcache: dm_io_client_create() error";
		goto bad4;
	}

	memset(fcc->copying, 0 ,bitmap_size);
	if (argc>=4 && strcmp(argv[3], "create")==0)
	{	// create new cache
		memset(fcc->bitmap, 0, bitmap_size);
		r=write_ender(fcc);
		if (r!=0)
		{
			ti->error = "dm-foolcache: ender write error";
			goto bad5;
		}
	}
	else
	{	// open existing cache
		r=read_ender(fcc);
		if (r!=0)
		{
			ti->error = "dm-foolcache: ender read error";
			goto bad5;
		}
	}

	init_completion(&fcc->copied);

	ti->num_flush_requests = 1;
	ti->num_discard_requests = 1;
	ti->private = fcc;
	return 0;

bad5:
	dm_io_client_destroy(fcc->io_client);
bad4:
	if (fcc->bitmap) kfree(fcc->bitmap);
	if (fcc->copying) kfree(fcc->copying);
bad3:
	dm_put_device(ti, fcc->cache);
bad2:
	dm_put_device(ti, fcc->origin);
bad1:
	kfree(fcc);
	return -EINVAL;
}

static void foolcache_dtr(struct dm_target *ti)
{
	struct foolcache_c *fcc = ti->private;
	kfree(fcc->bitmap);
	kfree(fcc->copying);
	dm_io_client_destroy(fcc->io_client);
	dm_put_device(ti, fcc->cache);
	dm_put_device(ti, fcc->origin);
	kfree(fcc);
}

static int foolcache_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	struct foolcache_c *fcc = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s %s %u", fcc->origin->name, 
			fcc->cache->name, fcc->block_size*(1024/512));
		break;
	}
	return 0;
}

static int foolcache_ioctl(struct dm_target *ti, unsigned int cmd,
			unsigned long arg)
{
	struct foolcache_c *fcc = ti->private;
	int r = 0;

	// if (cmd==FIEMAP)
	// {

	// }

	return r;
}
/*
static int foolcache_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
			struct bio_vec *biovec, int max_size)
{
	struct foolcache_c *fcc = ti->private;
	struct request_queue *q = bdev_get_queue(fcc->dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = fcc->dev->bdev;
	bvm->bi_sector = linear_map_sector(ti, bvm->bi_sector);

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int foolcache_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct foolcache_c *fcc = ti->private;

	return fn(ti, fcc->dev, fcc->start, ti->len, data);
}
*/

static int foolcache_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	struct foolcache_c *fcc = ti->private;
	return foolcache_map_sync(fcc, bio);
}

static struct target_type foolcache_target = {
	.name   = "foolcache",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr    = foolcache_ctr,
	.dtr    = foolcache_dtr,
	.map    = foolcache_map,
	.status = foolcache_status,
	.ioctl  = foolcache_ioctl,
//	.merge  = foolcache_merge,
//	.iterate_devices = foolcache_iterate_devices,
};

int __init dm_foolcache_init(void)
{
	int r = dm_register_target(&foolcache_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

void dm_foolcache_exit(void)
{
	dm_unregister_target(&foolcache_target);
}


/* Module hooks */
module_init(dm_foolcache_init);
module_exit(dm_foolcache_exit);

MODULE_DESCRIPTION(DM_NAME " foolcache target");
MODULE_AUTHOR("Huiba Li <lihuiba@gmail.com>");
MODULE_LICENSE("GPL");

