#!/bin/sh

# Test the NPF packet filter in the LWIP service through /dev/npf.  Needs
# root.  The test switches the filter off again when it is done.

PATH=/bin:/usr/bin:/sbin:/usr/sbin
export PATH

echo -n "Test npf "

cd npf || exit 1

out=`./npfdev_test 2>&1` || { echo "npfdev_test failed:"; echo "$out"; exit 1; }
echo "$out" | tail -n 1 | grep -q '^ok' || { echo "npfdev_test:"; echo "$out"; exit 1; }

echo "ok"
exit 0
