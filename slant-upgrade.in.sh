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

if [ ! -f "@DATADIR@/slant.db" ]
then
	# If the database doesn't exist, obviously nothing's running.
	# Simply install it and exit.
	set -e
	mkdir -p "@DATADIR@"
	echo "@DATADIR@/slant.db: installing new"
	kwebapp-sql "@SHAREDIR@/slant/slant.kwbp" | sqlite3 "@DATADIR@/slant.db"
	chown www "@DATADIR@/slant.db"
	chmod 600 "@DATADIR@/slant.db"
	install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "@DATADIR@/slant.kwbp"
	chmod 555 "@CGIBIN@/slant-cgi"
	exit 0
fi

# If the database exists, make sure we're not still writing to it.
# First try to use rcctl; then make sure we're not running debug
# instances.

stopped=0
rcctl check slant >/dev/null
if [ $? -eq 0 ]
then
	echo "slant(rcctl) running: stopping it first" 1>&2
	rcctl stop slant
	if [ $? -ne 0 ]
	then
		echo "slant(rcctl) cannot be stopped" 1>&2
		exit 1
	fi
	stopped=1
else
	pgrep slant-collectd >/dev/null
	if [ $? -eq 0 ]
	then
		echo "slant-collectd running: stopping it first" 1>&2
		stopped=2
		pkill slant-collectd
		sleep 2
		pgrep slant-collectd >/dev/null
		if [ $? -eq 0 ]
		then
			echo "slant-collectd cannot be stopped" 1>&2
			exit 1
		fi
	fi
fi

set -e

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

if [ "$stopped" -eq 1 ]
then
	echo "slant(rcctl): restarting" 1>&2
	rcctl start slant
elif [ "$stopped" -eq 2 ]
then
	echo "slant-collectd must be manually restarted" 1>&2
fi
