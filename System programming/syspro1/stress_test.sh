#!/bin/bash
set -e
BASE="logs"
IN="jms_in"
OUT="jms_out"

echo "/BUILD/"
make clean && make

rm -rf "$BASE"
mkdir -p "$BASE"
rm -f "$IN" "$OUT"

echo "/START COORD/"
./jms_coord -l "$BASE" -n 3 &
COORD_PID=$!
sleep 1

echo "/RUN CONSOLE/"
./jms_console -w "$IN" -r "$OUT"
wait $COORD_PID || true
echo "/DONE/"