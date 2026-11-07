#!/bin/bash
# run-boot.sh <distDir> <tag> <outDir> — one x64 cold-start boot of the
# given dist under the project's standard live measurement protocol
# (preserved live driver, fresh profile, bwretry=2, 900 s window,
# hardened FD-47 classification), served locally from <distDir>.
set -u
DIST=$1; TAG=$2; OUT=$3
D=/home/ubuntu/winbox-project/worktrees/wt-coldstart-x64/test-results/coldstart-x64
cd $D
rm -f serve-$TAG.log
node serve-dist.mjs "$DIST" > serve-$TAG.log 2>&1 &
SRVPID=$!
for i in $(seq 1 100); do grep -q "^BASE=" serve-$TAG.log 2>/dev/null && break; sleep 0.2; done
BASE=$(grep "^BASE=" serve-$TAG.log | cut -d= -f2-)
echo "boot tag=$TAG base=$BASE dist=$DIST"
node live-driver.mjs --base=$BASE --arch=x64 --n=1 --budget-ms=900000 --tag=$TAG --out=$OUT
RC=$?
kill $SRVPID 2>/dev/null
wait $SRVPID 2>/dev/null
exit $RC
