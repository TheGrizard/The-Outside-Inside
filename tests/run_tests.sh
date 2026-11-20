#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The simulator's checks: the controls, the lists, saving across a restart (and a flash that refuses), the screen
# reader. Each script fails the run on a FAIL line.
set -e
cd "$(dirname "$0")/.."
./build.sh --host
out=app/build/host/shots
mkdir -p "$out"
fail=0
for t in app/tests/*.txt; do
    echo "== $t"
    if app/build/host/the-outside-inside_host "$t" "$out" | tee /dev/stderr | grep -q FAIL; then fail=1; fi
done
exit $fail
