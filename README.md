snapcache
=========

A snapshot+cache module for device mapper, which has the following features:

1. Copy-on-Read(CoR), which copies data blocks when it is read.

2. Copy-on-Write(CoW), which copies data when an partial block is read.

2. Separate meta-data storage, which allows the cache storage to be used alone as 
   a raw copy, if the full content has been read (copyied).
