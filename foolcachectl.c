#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/fiemap.h>
#include "ioctl.h"

const char* error = NULL;

struct foolcache {
	int fd;
	size_t size, blocks;
	unsigned int blocksize;
};

void replicate(int fd)
{

}

foolcache* foolcache_ctor(const char* path)
{
	int ret;
	foolcache* fc;
	struct stat stat;
	fc = malloc(sizeof(*fc));
	if (fc==NULL) return NULL;

	fcc->fd = open(path, "rb");
	if (fcc->fd==-1)
	{
		error = "open failed";
		goto out1;
	}

	ret = fstat(fd, &stat);
	if (ret) 
	{
		error = "fstat failed"
		goto out2;
	}
	fc->size = stat.st_size;

	ret = ioctl(fd, FOOLCACHE_GETBSZ, &fc->blocksize);
	if (ret==-1)
	{
		error = strerror(errno);
		goto out2;
	}
	fc->blocks = fc->size / fc->blocksize;
	
	return fc;


out2:
	close(fc->fd);
out1:
	free(fc);
	return NULL;
}

int foolcache_fibmap(struct foolcache* fc, int block)
{
	int ret = ioctl(fd, FOOLCACHE_FIBMAP, &block);
	if (ret==-1) return -1;
	return block;
}

int main(int argc, char** argv)
{
	char* error;
	int fd, ret, blocksize=0, i, block;
	struct fiemap fiemap;
	struct fiemap* fmap;
	struct foolcache* fc;
	fc = foolcache_ctor(argv[1]);
	if (fc==NULL)
	{
		puts(error);
		return -1;
	}

	printf("Block Size: %dKB\n", fc->blocksize/1024);

	for (i=0; 1; ++i)
	{
		block = i;
		ret = ioctl(fd, FOOLCACHE_FIBMAP, &block);
		if (ret==-1) break;
		printf("%u", block);
	}
	printf("\n");

	fiemap.fm_start = 0;
	fiemap.fm_length = 1024*1024*1024;
	fiemap.fm_extent_count = 0;
	ret = ioctl(fd, FOOLCACHE_FIEMAP, &fiemap);
	printf("fm_mapped_extents=%u\n", fiemap.fm_mapped_extents);

	fmap = malloc(sizeof(struct fiemap) + 
		fiemap.fm_mapped_extents * sizeof(struct fiemap_extent));
	fmap->fm_start = fiemap.fm_start;
	fmap->fm_length = fiemap.fm_length;
	fmap->fm_extent_count = fiemap.fm_mapped_extents;
	ret = ioctl(fd, FOOLCACHE_FIEMAP, fmap);
	printf("fm_mapped_extents=%u\n", fmap->fm_mapped_extents);

	for (i=0; i<fmap->fm_mapped_extents; ++i)
	{
		struct fiemap_extent* e = &fmap->fm_extents[i];
		printf("%lu: [%lu, %lu]\n", 
			(unsigned long)(e->fe_logical/e->fe_length),
			(unsigned long)e->fe_logical,
			(unsigned long)(e->fe_logical + e->fe_length - 1));
	}

	free(fmap);
	close(fd);
	return 0;
}

