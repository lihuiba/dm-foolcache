# set -x

deconfigramdisk() {
	dmsetup remove fcdev
	rmmod dm_foolcache
	losetup -d /dev/loop0 /dev/loop1
	rm slow ram/fast
	umount ram
	rmdir ram
}

configramdisk() {
	size=`expr $1 \* 2097152`
	fcbs=$2
	mkdir ram
	mount none ram -t tmpfs -o size=90%
	dd if=/dev/zero of=ram/fast bs=512 count=0 seek=${size}
	dd if=/dev/zero of=slow bs=512 count=0 seek=${size}
	losetup /dev/loop0 slow
	losetup /dev/loop1 ram/fast
	insmod ./dm-foolcache.ko
	echo "0 $size foolcache /dev/loop0 /dev/loop1 $fcbs create" | dmsetup create fcdev
}

deconfigzero() {
	dmsetup remove fcdev
	dmsetup remove fast
	dmsetup remove slow
	rmmod dm_foolcache
}

configzero() {
	size=`expr $1 \* 2097152`
	fcbs=$2
	modprobe dm-zero
	insmod ./dm-foolcache.ko
	echo "0 $size zero" | dmsetup create fast
	echo "0 $size zero" | dmsetup create slow
	echo "0 $size foolcache /dev/mapper/slow /dev/mapper/fast $fcbs create" | dmsetup create fcdev
}

config() {
	size=${1:-8}		# 8GB size by default
	fcbs=${2:-1024}		# 1MB block size by default
	configzero $size $fcbs
}

deconfig() {
	deconfigzero
}


size=8 #GB
deconfig
threads=`cat /proc/cpuinfo | grep processor | wc -l`
echo -n "Found $threads hardware threads, "
threads=`expr $threads \* 2`
echo "will spawn $threads OS threads for each following test."
# for fcbs in 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
for fcbs in 256 512 1024 2048 4096 8192 16384 32768
do
	for fiobs in 512 1k 2k 4k 8k 16k 32k 64k 128k 256k 512k 1024k 2048k 4096k
	# for fiobs in 1024k 2048k 4096k
	do
		echo "********************** volume size = $size GB,  fcbs = $fcbs KB,  fiobs = $fiobs *********************************"
		config 8 $fcbs
		fio --filename=/dev/mapper/fcdev --direct=1 --thread --iodepth 128 --thread --rw=randread --ioengine=libaio --size=100% --numjobs=$threads --bs=$fiobs --group_reporting --name=mytest 
		cat /proc/foolcache/*
		deconfig
		sync
	done
done
