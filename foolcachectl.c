#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fiemap.h>
#include "ioctl.h"

int main(int argc, char** argv)
{
	char* error;
	int fd, ret, blocksize=0, i, block;
	struct fiemap fiemap;
	struct fiemap* fmap;
	fd = open(argv[1], "rb");
	if (fd==-1)
	{
		printf("open failed\n");
		return -1;
	}

	ret = ioctl(fd, FOOLCACHE_GETBSZ, &blocksize);
	if (ret==-1)
	{
		error = strerror(errno);
		printf("errno=%d (%s)\n", errno, error);
		close(fd);
		return -1;	
	}
	printf("Block Size: %dKB\n", blocksize/1024);

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