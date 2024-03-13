#!/bin/sh

# TODO: make this more robust

# pgrep keyd && { echo "Stop keyd before running tests"; exit -1; }

[ "$(id -u)" -eq 0 ] && root=true

tmpdir=$(mktemp -d)

cleanup() {
	rm -rf "$tmpdir"
	kill $pid

	trap - EXIT
	exit
}

trap cleanup INT

cd "$(dirname "$0")"
cp test.conf "$tmpdir"

(cd ..;make CONFIG_DIR="$tmpdir") || exit -1
if [ "$root" ]; then
	../bin/keyd > test.log 2>&1 &
else
	../bin/keyd -s "$tmpdir/socket" > test.log 2>&1 &
fi


pid=$!

sleep .7s
if [ $# -ne 0 ]; then
	test_files="$(echo "$@"|sed -e 's/ /.t /g').t"
	./runner.py -v $test_files
	cleanup
fi

./runner.py -ev *.t
cleanup
