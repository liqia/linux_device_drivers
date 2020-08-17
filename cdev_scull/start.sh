#!/bin/sh
module="scull"
device="scull"
mode="664"
make
insmod scull.ko
rm -rf /dev/${device}
mknod /dev/${device} c 240 0

