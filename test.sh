set -x
# dd if=/dev/zero of=slow bs=1M count=0 seek=1k
# dd if=/dev/zero of=fast bs=1M count=0 seek=1k
losetup /dev/loop0 slow
losetup /dev/loop1 fast

rmmod dm_foolcache
insmod ./dm-foolcache.ko

echo "0 `blockdev --getsize /dev/loop0` foolcache /dev/loop0 /dev/loop1 1024 create" | dmsetup create fcdev

