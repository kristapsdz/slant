#! /bin/sh
#
#	$Id$
#
# Copyright (c) 2018 Kristaps Dzonsons <kristaps@bsd.lv>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

KWBP="@SHAREDIR@/slant/slant.kwbp"

args=`getopt f: $*`
if [ $? -ne 0 ]
then
	echo 'Usage: ...'
	exit 2
fi
set -- $args
while [ $# -ne 0 ]
do
	case "$1" in
	-f)
		KWBP="$2"; shift; shift;;
	--)
		shift; break;;
        esac
done

echo "$KWBP"

if [ ! -f "@DATADIR@/slant.db" ]
then
	# If the database doesn't exist, obviously nothing's running.
	# Simply install it and exit.
	set -e
	mkdir -p "@DATADIR@"
	echo "@DATADIR@/slant.db: installing new"
	ort-sql "$KWBP" | sqlite3 "@DATADIR@/slant.db"
	chown www "@DATADIR@/slant.db"
	chmod 600 "@DATADIR@/slant.db"
	install -m 0444 "$KWBP" "@DATADIR@/slant.kwbp"
	exit 0
fi

set -e
TMPFILE=`mktemp` || exit 1
trap "rm -f $TMPFILE" ERR EXIT

echo "@DATADIR@/slant.db: patching existing"

( echo "BEGIN EXCLUSIVE TRANSACTION;" ; \
  ort-sqldiff "@DATADIR@/slant.kwbp"  "$KWBP" ; \
  echo "COMMIT TRANSACTION;" ; ) > $TMPFILE

if [ $? -ne 0 ]
then
	echo "@DATADIR@/slant.db: patch aborted" 1>&2
	exit 1
fi

sqlite3 "@DATADIR@/slant.db" < $TMPFILE
install -m 0444 "$KWBP" "@DATADIR@/slant.kwbp"
rm -f "@DATADIR@/slant-upgrade.sql"
echo "@DATADIR@/slant.db: patch success"
