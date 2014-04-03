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
#include <linux/dm-kcopyd.h>
#include <linux/dm-io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define DM_MSG_PREFIX "foolcache"

 /* TODOs:
merge the bitmaps



 */

const static char SIGNATURE[]="FOOLCACHE";
struct header {
	char signature[sizeof(SIGNATURE)];
	unsigned int block_size;
};

struct foolcache_c {
	struct dm_dev* cache;
	struct dm_dev* origin;
	struct dm_io_client* io_client;
	unsigned int bypassing;
	sector_t sectors, last_caching_sector;
	unsigned int block_size;		// block (chunk) size, in sector
	unsigned int block_shift;
	unsigned int block_mask;
	unsigned long* bitmap;
	unsigned long* copying;
	unsigned long bitmap_modified;
	unsigned long bitmap_last_sync;
	struct header* header;
	unsigned int bitmap_sectors;
	struct completion copied;
	struct dm_kcopyd_client* kcopyd_client;
	unsigned long cached_blocks, hits, misses;
};

struct job2_kcopyd {
	struct foolcache_c* fcc;
	unsigned long block;
	struct dm_io_region origin, cache;
};

static struct proc_dir_entry* fcdir_proc;
static inline void proc_new_entry(struct foolcache_c* fcc);
static inline void proc_remove_entry(struct foolcache_c* fcc);

static inline unsigned long sector2block(struct foolcache_c* fcc, sector_t sector)
{
	return sector >> fcc->block_shift;
}

static inline sector_t block2sector(struct foolcache_c* fcc, unsigned long block)
{
	return block << fcc->block_shift;
}

inline unsigned char cout_bits_uchar(unsigned char x)
{
	return (x&1) + ((x>>1)&1) + ((x>>2)&1) + ((x>>3)&1)
		 + ((x>>4)&1) + ((x>>5)&1) + ((x>>6)&1) + ((x>>7));
}

static unsigned long count_bits(unsigned long* buf, unsigned long size)
{
	unsigned long i, r;
	unsigned char table[256];
	unsigned char* _buf = (unsigned char*)buf;
	for (i=0; i<256; ++i)
	{
		table[i] = cout_bits_uchar(i);
	}
	for (i=r=0; i<size/sizeof(_buf[0]); ++i)
	{
		r+=table[_buf[i]];
	}
	for (i*=sizeof(_buf[0]); i<size; ++i)
	{
		r+=test_bit(i, buf);
	}
	return r;
}

static int write_bitmap(struct foolcache_c* fcc)
{
	int r;
	struct dm_io_region region;
	struct dm_io_request io_req;
	
	if (fcc->bitmap_modified == 0)
	{
		return 0;
	}

	region.bdev = fcc->cache->bdev;
	region.sector = fcc->sectors - (fcc->bitmap_sectors + 1);
	region.count = fcc->bitmap_sectors;
	io_req.bi_rw = WRITE;
	io_req.mem.type = DM_IO_VMA;
	io_req.mem.ptr.vma = fcc->bitmap;
	io_req.notify.fn = NULL;
	// io_req.notify.context = NULL;
	io_req.client = fcc->io_client;

	r=dm_io(&io_req, 1, &region, NULL);
	if (r!=0) return r;
	fcc->bitmap_modified = 0;
	return 0;
}

static int write_header(struct foolcache_c* fcc)
{
	int r;
	struct dm_io_region region = {
		.bdev = fcc->cache->bdev,
		.sector = fcc->sectors - 1,
		.count = 1,
	};
	struct dm_io_request io_req = {
		.bi_rw = WRITE,
		.mem.type = DM_IO_VMA,
		.mem.ptr.vma = fcc->header,
		// .notify.fn = ,
		// .notify.context = ,
		.client = fcc->io_client,
	};

	memcpy(fcc->header->signature, SIGNATURE, sizeof(SIGNATURE));
	fcc->header->block_size = fcc->block_size;
	r = dm_io(&io_req, 1, &region, NULL);
	return r;
}

static inline int write_ender(struct foolcache_c* fcc)
{
	return write_bitmap(fcc) || write_header(fcc);
}

static int read_ender(struct foolcache_c* fcc)
{
	int r;
	struct dm_io_region region = {
		.bdev = fcc->cache->bdev,
		.sector = fcc->sectors - 1,
		.count = 1,
	};
	struct dm_io_request io_req = {
		.bi_rw = READ,
		.mem.type = DM_IO_VMA,
		.mem.ptr.vma = fcc->header,
		// .notify.fn = ,
		// .notify.context = ,
		.client = fcc->io_client,
	};
	r=dm_io(&io_req, 1, &region, NULL);
	if (r!=0) return r;

	r=strncmp(fcc->header->signature, SIGNATURE, sizeof(SIGNATURE)-1);
	if (r!=0) return r;
	if (fcc->header->block_size != fcc->block_size) return -EINVAL;

	io_req.mem.ptr.addr = fcc->bitmap;
	region.sector = fcc->last_caching_sector + 1;
	region.count = fcc->bitmap_sectors;
	r = dm_io(&io_req, 1, &region, NULL);
	if (r==0) fcc->bitmap_modified = 0;
	return r;
}

void copy_block_callback(int read_err, unsigned long write_err, void *context)
{
	struct job2_kcopyd* job = context;
	struct foolcache_c* fcc = job->fcc;
	unsigned long block = job->block;
	if (read_err || write_err)
	{
		fcc->bypassing = 1;
	}
	else
	{
		set_bit(block, fcc->bitmap);
		fcc->cached_blocks++;
		fcc->bitmap_modified = 1;
	}

	clear_bit(block, fcc->copying);
	complete_all(&fcc->copied);
}

static int ensure_block(struct foolcache_c* fcc, unsigned int block)
{
	struct job2_kcopyd job;

	if (fcc->bypassing) return 0;
	if (test_bit(block, fcc->bitmap))
	{
		fcc->hits++;
		return 0;
	}

	// before copying
	if (test_and_set_bit(block, fcc->copying))
	{	// the block is being copied by another thread, let's just wait
		fcc->hits++;
wait:
		//printk("dm-foolcache: pre-wait\n");
		wait_for_completion_timeout(&fcc->copied, 1*HZ);
		//printk("dm-foolcache: post-wait\n");
		if (fcc->bypassing)
		{
			return -EIO;
		}
		if (test_bit(block, fcc->copying))
		{
			goto wait;
		}
		return 0;
	}

	if (test_bit(block, fcc->bitmap))
	{
		fcc->hits++;
		clear_bit(block, fcc->copying);
		return 0;
	}

	// do copying
	fcc->misses++;
	job.fcc = fcc;
	job.block = block;
	job.origin.bdev = fcc->origin->bdev;
	job.cache.bdev = fcc->cache->bdev;
	job.origin.sector = job.cache.sector = block2sector(fcc, block);
	job.origin.count = job.cache.count = fcc->block_size;	
	dm_kcopyd_copy(fcc->kcopyd_client, &job.origin, 1, &job.cache, 
		0, copy_block_callback, &job);
	goto wait;
}

static int foolcache_map_sync(struct foolcache_c* fcc, struct bio* bio)
{
	sector_t last_sector;
	u64 now = get_jiffies_64();
	if (fcc->bitmap_last_sync+HZ*10 < now || now < fcc->bitmap_last_sync)
	{
		write_bitmap(fcc);
	}

	if (bio_data_dir(bio) == WRITE)
	{
		return -EIO;
	}

	last_sector = bio->bi_sector + bio->bi_size/512 - 1;
	if (unlikely(fcc->bypassing || last_sector > fcc->last_caching_sector))
	{
		bio->bi_bdev = fcc->origin->bdev;
	}
	else
	{	// preparing the cache, followed by remapping
		unsigned long end_block = sector2block(fcc, last_sector);
		unsigned long i = sector2block(fcc, bio->bi_sector);
		for (; i<=end_block; ++i)
		{
			ensure_block(fcc, i);
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

	fcc = vzalloc(sizeof(*fcc));
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
	printk("dm-foolcache: bs %uKB\n", bs);

	bs*=(1024/512); // KB to sector
	fcc->block_size = bs;
	fcc->block_shift = ffs(bs)-1;
	fcc->block_mask = ~(bs-1);
	printk("dm-foolcache: bshift %u, bmask %u\n", fcc->block_shift, fcc->block_mask);
	fcc->bitmap_sectors = fcc->sectors/bs/8/512 + 1; 	// sizeof bitmap, in sector
	fcc->last_caching_sector = fcc->sectors - 1 - 1 - fcc->bitmap_sectors;
	bitmap_size = fcc->bitmap_sectors*512;
	fcc->bitmap = vzalloc(bitmap_size);
	fcc->copying = vzalloc(bitmap_size);
	fcc->header = vzalloc(512);
	if (fcc->bitmap==NULL || fcc->copying==NULL || fcc->header==NULL)
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

	fcc->kcopyd_client = dm_kcopyd_client_create();
	if (IS_ERR(fcc->kcopyd_client))
	{
		ti->error = "dm-foolcache: dm_kcopyd_client_create() error";
		goto bad5;
	}

	fcc->hits = 0;
	fcc->misses = 0;
	memset(fcc->copying, 0, bitmap_size);
	if (argc>=4 && strcmp(argv[3], "create")==0)
	{	// create new cache
		memset(fcc->bitmap, 0, bitmap_size);
		fcc->bitmap_modified = 1;
		fcc->cached_blocks = 0;
		r = write_ender(fcc);
		if (r!=0)
		{
			ti->error = "dm-foolcache: ender write error";
			goto bad6;
		}
	}
	else
	{	// open existing cache
		r = read_ender(fcc);
		fcc->cached_blocks = count_bits(fcc->bitmap, fcc->sectors);
		if (r!=0)
		{
			ti->error = "dm-foolcache: ender read error";
			goto bad6;
		}
	}
	fcc->bitmap_last_sync = jiffies;

	init_completion(&fcc->copied);
	proc_new_entry(fcc);

	ti->num_flush_requests = 1;
	ti->num_discard_requests = 1;
	ti->private = fcc;
	printk("dm-foolcache: ctor succeeed\n");
	return 0;

bad6:
	dm_kcopyd_client_destroy(fcc->kcopyd_client);
bad5:
	dm_io_client_destroy(fcc->io_client);
bad4:
	if (fcc->bitmap) vfree(fcc->bitmap);
	if (fcc->copying) vfree(fcc->copying);
	if (fcc->header) vfree(fcc->header);
bad3:
	dm_put_device(ti, fcc->cache);
bad2:
	dm_put_device(ti, fcc->origin);
bad1:
	vfree(fcc);
	printk("dm-foolcache: ctor failed\n");
	return -EINVAL;
}

static void foolcache_dtr(struct dm_target *ti)
{
	struct foolcache_c *fcc = ti->private;
	write_bitmap(fcc);
	vfree(fcc->bitmap);
	vfree(fcc->copying);
	vfree(fcc->header);
	proc_remove_entry(fcc);
	dm_kcopyd_client_destroy(fcc->kcopyd_client);
	dm_io_client_destroy(fcc->io_client);
	dm_put_device(ti, fcc->origin);
	dm_put_device(ti, fcc->cache);
	vfree(fcc);
}

static void foolcache_status(struct dm_target *ti, status_type_t type,
		char *result, unsigned int maxlen)
{
	struct foolcache_c *fcc = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s %s %u", fcc->origin->name, 
			fcc->cache->name, fcc->block_size*512/1024);
		break;
	}
}

static int foolcache_ioctl(struct dm_target *ti, unsigned int cmd,
			unsigned long arg)
{
//	struct foolcache_c *fcc = ti->private;
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
*/
static int foolcache_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	int r;
	struct foolcache_c *fcc = ti->private;
	r = fn(ti, fcc->origin, 0, fcc->sectors, data);
	if (r) return r;
	r = fn(ti, fcc->cache, 0, fcc->sectors, data);
	return r;
}


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
	.iterate_devices = foolcache_iterate_devices,
};

static inline void print_percent(struct seq_file *m, const char* title, 
	unsigned long a, unsigned long b)
{
	unsigned int x = b ? a*100/b : 0;
	unsigned int y = b ? (a*1000/b)%10 : 0;
	seq_printf(m, "%s: %lu/%lu (%u.%u%%)\n", title, a, b, x, y);
}

static int foolcache_proc_show(struct seq_file *m, void *v)
{
	struct foolcache_c *fcc = v;
	unsigned long blocks;
	if (fcc==NULL)
	{
		seq_puts(m, "NULL");
		return 0;
	}

	blocks = sector2block(fcc, fcc->sectors) + 1;
	print_percent(m, "Hit", fcc->hits, fcc->hits + fcc->misses);
	print_percent(m, "Fullfillment", fcc->cached_blocks, blocks);
	return 0;
}

static int foolcache_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, foolcache_proc_show, PDE(inode)->data);
}

static const struct file_operations foolcache_proc_fops = {
	.open		= foolcache_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static inline void proc_new_entry(struct foolcache_c* fcc)
{
// proc_create_data(const char *name, umode_t mode,
// 					struct proc_dir_entry *parent,
// 					const struct file_operations *proc_fops,
// 					void *data)
	proc_create_data(fcc->origin->name, 
		S_IRUGO, fcdir_proc, &foolcache_proc_fops, fcc);
}

static inline void proc_remove_entry(struct foolcache_c* fcc)
{
	remove_proc_entry(fcc->origin->name, fcdir_proc);
}

int __init dm_foolcache_init(void)
{
	int r = dm_register_target(&foolcache_target);
	if (r < 0)
	{
		DMERR("register failed %d", r);
	}

	fcdir_proc = proc_mkdir("foolcache", NULL);

	return r;
}

void dm_foolcache_exit(void)
{
	dm_unregister_target(&foolcache_target);
	remove_proc_entry("foolcache", NULL);
}


/* Module hooks */
module_init(dm_foolcache_init);
module_exit(dm_foolcache_exit);

MODULE_DESCRIPTION(DM_NAME " foolcache target");
MODULE_AUTHOR("Huiba Li <lihuiba@gmail.com>");
MODULE_LICENSE("GPL");

