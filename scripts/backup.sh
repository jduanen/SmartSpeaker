#!/bin/sh
# Back up a stock unit before takeover: /opt/duer, /data/wifi, the U-Boot env
# and raw images of the env and app (/opt) partitions.
# Usage: scripts/backup.sh [dir]   (default backups/<serial>)
#
# Partition images are copied in 8 MB chunks through /tmp so the device's
# 128 MB of RAM is never asked to hold a whole image. /opt is mounted rw, so
# if DuerOS writes to it mid-copy the md5 check fails; stop DuerOS and rerun.
set -eu

SERIAL=$(adb get-serialno)
DIR=${1:-backups/$SERIAL}
mkdir -p "$DIR"

sh_() { adb shell "$1" | tr -d '\r'; }

echo "== files"
adb pull /opt/duer "$DIR/duer"
adb pull /data/wifi "$DIR/wifi"
sh_ fw_printenv >"$DIR/fw_printenv.txt"

dump() {
  part=$1
  dev=$(sh_ "readlink -f /dev/by-name/$part")
  kb=$(sh_ "awk '\$4==\"${dev##*/}\"{print \$3}' /proc/partitions")
  [ -n "$kb" ] || { echo "cannot size $part ($dev)"; exit 1; }
  echo "== $part ($dev, $kb KB)"
  img="$DIR/$part.img"
  : >"$img"
  sh_ sync
  i=0
  while [ $((i * 8192)) -lt "$kb" ]; do
    sh_ "dd if=$dev of=/tmp/bk.chunk bs=64k count=128 skip=$((i * 128)) 2>/dev/null"
    adb pull /tmp/bk.chunk "$DIR/.chunk" >/dev/null
    cat "$DIR/.chunk" >>"$img"
    i=$((i + 1))
  done
  sh_ "rm -f /tmp/bk.chunk"
  rm -f "$DIR/.chunk"
  want=$(sh_ "md5sum $dev" | cut -d' ' -f1)
  got=$(md5sum "$img" | cut -d' ' -f1)
  [ "$want" = "$got" ] || { echo "md5 mismatch for $part: device $want host $got"; exit 1; }
  echo "$got  $part.img" >>"$DIR/md5sums.txt"
}

: >"$DIR/md5sums.txt"
dump env
dump app

echo "backup complete in $DIR"
