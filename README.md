Foolcache
============

Foolcache behaves like other block caches like *flashcache*, *dm-cache* or
*bcache*. The unique feature that foolcache provides is that, data stored
in cache media is organized exactly the same as in origin, so foolcache can
be used in conjunction with a background replicator.


Foolcache implements the following actions:

1. Copy-on-Read(CoR), which copies data when a block it is read.

2. Copy-on-Write(CoW), which copies data when a block is partially written.

3. Full-sized cache media, which drops the need for cache invalidation. 
"Full" and "fool" are homophonic.

Foolcache judges whether a block is cached or not by looking-up a bitmap,
and foolcache stores meta-data at the tail of the cache media. 
