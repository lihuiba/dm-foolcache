set -x
dmsetup remove fcdev
rmmod dm_foolcache
losetup -d loop0 loop1

