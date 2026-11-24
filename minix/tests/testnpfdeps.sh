#!/bin/sh

# Run the upstream unit tests of the libraries NPF is built on: nvlist,
# thmap, libqsbr (EBR/QSBR/GC) and liblpm.

PATH=/bin:/usr/bin:/sbin:/usr/sbin
export PATH

echo -n "Test npfdeps "

cd npfdeps || exit 1

for t in nvlist_add_test nvlist_append_test nvlist_exists_test nvlist_free_test \
    nvlist_get_test nvlist_move_test; do
	# TAP output: fail on any "not ok".
	out=`./$t 2>&1` || { echo "$t failed:"; echo "$out"; exit 1; }
	if echo "$out" | grep -q '^not ok'; then
		echo "$t:"; echo "$out" | grep '^not ok'; exit 1
	fi
done

# Each prints "ok" at the end, and aborts on a failed assertion.
for t in t_thmap thmap_stress t_gc t_lpm; do
	out=`./$t 2>&1` || { echo "$t failed:"; echo "$out"; exit 1; }
	echo "$out" | tail -n 1 | grep -q '^ok' || \
	    { echo "$t:"; echo "$out"; exit 1; }
done

# qsbr_stress passes even when its writer starves, so check that its EBR
# and G/C phases get real work done: millions of objects in 2 s.  Its QSBR
# phase needs only to make progress: every reader busy-loops without
# blocking, and MINIX gives user threads only the non-boot CPUs and a 200 ms
# quantum, so with more threads than those CPUs a QSBR grace period can cost
# a reader's whole quantum (about 10 per run).  The phase still checks that
# no reader sees a destroyed object.
out=`./qsbr_stress 2 2>&1` || { echo "qsbr_stress failed:"; echo "$out"; exit 1; }
echo "$out" | tail -n 1 | grep -q '^ok' || { echo "qsbr_stress:"; echo "$out"; exit 1; }
set -- `echo "$out" | sed -n 's/^# //p'`
[ $# -eq 3 ] && [ "$1" -ge 10000 ] && [ "$2" -ge 1 ] && [ "$3" -ge 10000 ] || \
    { echo "qsbr_stress starved:"; echo "$out"; exit 1; }

echo "ok"
exit 0
