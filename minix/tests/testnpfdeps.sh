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
for t in t_thmap thmap_stress t_gc "qsbr_stress 2" t_lpm; do
	out=`./$t 2>&1` || { echo "$t failed:"; echo "$out"; exit 1; }
	echo "$out" | tail -n 1 | grep -q '^ok' || \
	    { echo "$t:"; echo "$out"; exit 1; }
done

echo "ok"
exit 0
