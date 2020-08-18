#!/bin/sh
# $Id: scull_load,v 1.4 2004/11/03 06:19:49 rubini Exp $
module="scullpipe"
device="scull_pipe"
mode="664"

# invoke insmod with all arguments we got
# and use a pathname, as insmod doesn't look in . by default
echo $1

/sbin/insmod ./$module.ko $1 || exit 1

# retrieve major number
major=$(awk "\$2==\"$device\" {print \$1}" /proc/devices)


# Remove stale nodes and replace them, then give gid and perms
# Usually the script is shorter, it's scull that has several devices in it.
rm -f /dev/${device}
mknod /dev/${device} c $major 0