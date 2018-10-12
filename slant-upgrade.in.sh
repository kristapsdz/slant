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

stopped=
pgrep slant-collectd >/dev/null
if [ $? -ne 1 ]
then
	echo "slant-collectd running: stopping it first" 1>&2
	pkill slant-collectd
	sleep 2
	pgrep slant-collectd >/dev/null
	if [ $? -ne 1 ]
	then
		echo "slant-collectd: not dying: trying again" 1>&2
		pkill slant-collectd
		sleep 5
		pgrep slant-collectd >/dev/null
		if [ $? -ne 1 ]
		then
			echo "slant-collectd: not dying" 1>&2
			exit 1
		fi
	fi
	stopped=1
fi

set -e

if [ ! -f "@DATADIR@/slant.db" ]
then
	mkdir -p "@DATADIR@"
	echo "@DATADIR@/slant.db: installing new"
	kwebapp-sql "@SHAREDIR@/slant/slant.kwbp" | sqlite3 "@DATADIR@/slant.db"
	chown www "@DATADIR@/slant.db"
	chmod 600 "@DATADIR@/slant.db"
	install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "@DATADIR@/slant.kwbp"
	chmod 555 "@CGIBIN@/slant-cgi"
	exit 0
fi

TMPFILE=`mktemp` || exit 1
trap "rm -f $TMPFILE" ERR EXIT

echo "@DATADIR@/slant.db: patching existing"

( echo "BEGIN EXCLUSIVE TRANSACTION;" ; \
  kwebapp-sqldiff "@DATADIR@/slant.kwbp"  "@SHAREDIR@/slant/slant.kwbp" ; \
  echo "COMMIT TRANSACTION;" ; ) > $TMPFILE

if [ $? -ne 0 ]
then
	echo "@DATADIR@/slant.db: patch aborted" 1>&2
	exit 1
fi

sqlite3 "@DATADIR@/slant.db" < $TMPFILE
install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "@DATADIR@/slant.kwbp"
chmod 555 "@CGIBIN@/slant-cgi"
rm -f "@DATADIR@/slant-upgrade.sql"
echo "@DATADIR@/slant.db: patch success"

if [ -n "$stopped" ]
then
	echo "slant-collectd should be restarted"
fi
