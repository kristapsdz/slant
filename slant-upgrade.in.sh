#! /bin/sh

if [ -n "`pgrep slant-collectd`" ]
then
	echo "slant-collectd running: stop it first" 1>&2
	exit 1
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
rm -f "@DATADIR@/slant-upgrade.sql"
echo "@DATADIR@/slant.db: patch success"
