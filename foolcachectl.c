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
	fd = open(argv[1], "rb");
	ret = ioctl(fd, FOOLCACHE_GETBSZ, &blocksize);
	if (ret==-1)
	{
		error = strerror(errno);
		printf("errno=%d\n (%s)", errno, error);
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

	close(fd);
	return 0;
}