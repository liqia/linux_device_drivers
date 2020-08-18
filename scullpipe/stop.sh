#!/bin/sh
# $Id: scull_load,v 1.4 2004/11/03 06:19:49 rubini Exp $
module="scullpipe"
device="scull_pipe"

# invoke rmmod with all arguments we got
/sbin/rmmod $module $* || exit 1

rm -f /dev/${device}
