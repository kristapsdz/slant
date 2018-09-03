#! /bin/sh

if [ -n "`pgrep slant-collectd`" ]
then
	echo "slant-collectord running: stop it first" 1>&2
	exit 1
fi

set -e

if [ ! -f "@DATADIR@/slant.db" ]
then
	echo "@DATADIR@/slant.db: installing new"
	kwebapp-sql "@SHAREDIR@/slant/slant.kwbp" | sqlite3 "@DATADIR@/slant.db"
	install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "@DATADIR@/slant.kwbp"
	exit 0
fi

echo "@DATADIR@/slant.db: patching existing"

kwebapp-sqldiff "@DATADIR@/slant.kwbp"  "@SHAREDIR@/slant/slant.kwbp" > "@DATADIR@/slant-upgrade.sql"

if [ $? -ne 0 ]
then
	rm -f "@DATADIR@/slant-upgrade.sql"
	echo "@DATADIR@/slant.db: patch aborted" 1>&2
	exit 1
fi

sqlite3 "@DATADIR@/slant.db" < "@DATADIR@/slant-upgrade.sql"
install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "@DATADIR@/slant.kwbp"
rm -f "@DATADIR@/slant-upgrade.sql"
echo "@DATADIR@/slant.db: patch success"
