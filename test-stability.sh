# set -x

deconfig() {
	dmsetup remove fcdev
	rmmod dm_foolcache
	losetup -d /dev/loop0 /dev/loop1
	rm slow ram/fast
	umount ram
	rmdir ram
}

config() {
	fcbs=${1:-1024}
	mkdir ram
	mount none ram -t tmpfs -o size=90%
	dd if=/dev/zero of=ram/fast bs=1M count=0 seek=4k
	dd if=/dev/zero of=slow bs=1M count=0 seek=4k
	losetup /dev/loop0 slow
	losetup /dev/loop1 ram/fast
	insmod ./dm-foolcache.ko
	echo "0 `blockdev --getsize /dev/loop0` foolcache /dev/loop0 /dev/loop1 $fcbs create" | dmsetup create fcdev
}

# swapoff -a
deconfig
threads=`cat /proc/cpuinfo | grep processor | wc -l`
echo -n "Found $threads hardware threads, "
# threads=`expr $threads \* 2`
echo "will spawn $threads OS threads for each following test."
# for fcbs in 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
for fcbs in 32 64 128 256 512 1024 2048 4096 8192 16384 32768
do
	# for fiobs in 512 1k 2k 4k 8k 16k 32k 64k 128k 256k 512k 1024k 2048k 4096k
	for fiobs in 128k 256k 512k 1024k 2048k 4096k
	do
		echo "********************** fcbs = $fcbs KB,  fiobs = $fiobs *********************************"
		config $fcbs
		fio --filename=/dev/mapper/fcdev --direct=1 --thread --iodepth 128 --thread --rw=randread --ioengine=libaio --size=200% --numjobs=$threads --bs=$fiobs --group_reporting --name=mytest 
		cat /proc/foolcache/*
		deconfig
		sync
	done
done
