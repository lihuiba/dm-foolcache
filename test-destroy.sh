set -x
dmsetup remove fcdev
rmmod dm_foolcache
losetup -d /dev/loop0 /dev/loop1
rm slow fast
