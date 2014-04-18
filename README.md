Foolcache
============

Foolcache behaves like other block caches like *flashcache*, *dm-cache* or
*bcache*. The unique feature that foolcache provides is that, data stored
in cache media is organized exactly the same as in origin, so foolcache can
be used in conjunction with a background replicator.


Foolcache has the following features:

1. Copy-on-Read(CoR), which copies data when a block it is read.

2. The ability to transform a cache into a full replica, without data movement. 

3. Full-sized cache media, which eliminates the need for cache invalidation. 
"full" and "fool" are homophonic.

4. In the future, Copy-on-Write(CoW), which copies data when a block is 
partially written.


Foolcache judges whether a block has been cached or not by looking-up a bitmap,
and foolcache stores meta-data at the tail of the cache media. 
