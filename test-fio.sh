set -x
fio --filename=/dev/mapper/fcdev --direct=1 --iodepth 1 --thread --rw=randread --ioengine=psync --bs=4k --size=5G --numjobs=3 --runtime=10 --group_reporting --name=mytest 