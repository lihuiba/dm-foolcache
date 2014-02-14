dm-coa
=========

A copy-on-access module for device mapper, which has the following features:

1. Copy-on-Write(CoW), which copies data when a block is partially written.

2. Copy-on-Read(CoR), which copies data when a block it is read.

3. The same layout (almost) as origin, which allows the replica to be used as 
   a standalone copy, if the full content has been copied.
