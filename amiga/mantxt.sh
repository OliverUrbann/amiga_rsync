#!/bin/sh
# Render rsync.1.md and rsyncd.conf.5.md as plain 78-column text for the
# Amiga archive (no markdown, no control characters, no hyphenation).
#
# Needs pandoc, groff and col.  Usage: amiga/mantxt.sh <output-dir>
set -e

srcdir=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: mantxt.sh <output-dir>}
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
ver=$(sed -n 's/^#define RSYNC_VERSION "\(.*\)"/\1/p' "$srcdir/version.h")

for m in rsync.1 rsyncd.conf.5; do
	name=${m%.*}
	sec=${m##*.}
	# rsync writes each option as a list item "0." (md2man turns them into
	# headings); mark them so the numbers pandoc gives them can be removed.
	sed -E 's/^0\. +/0.  @@OPT@@/' "$srcdir/$m.md" > "$tmp/$m.md"
	pandoc -s -f markdown -t man --shift-heading-level-by=-1 \
		-M title="$(echo "$name" | tr a-z A-Z)" -M section="$sec" \
		-M header="rsync $ver" -M footer="rsync $ver" \
		"$tmp/$m.md" -o "$tmp/$m" 2>/dev/null
	sed -i 's/^\.hy$/.nh/' "$tmp/$m"
	groff -man -Tascii -rLL=78n -rIN=3n -dAD=l -P-c "$tmp/$m" 2>/dev/null \
		| col -bx \
		| sed -E 's/[[:space:]]+$//; s/^ *[0-9]+\. @@OPT@@/   /' \
		| cat -s > "$out/$m.txt"
	echo "$out/$m.txt"
done
