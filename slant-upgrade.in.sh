#! /bin/sh

if [ ! -f "/var/www/data/slant.db" ]
then
	echo "/var/www/data/slant.db: installing new"
	kwebapp-sql "@SHAREDIR@/slant/slant.kwbp" | sqlite3 "/var/www/data/slant.db"
	install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "/var/www/data/slant.kwbp"
	exit 0
fi

echo "/var/www/data/slant.db: patching existing"

kwebapp-sqldiff "/var/www/data/slant.kwbp"  "@SHAREDIR@/slant/slant.kwbp" > "/var/www/data/slant-upgrade.sql"

if [ $? -ne 0 ]
then
	rm -f "/var/www/data/slant-upgrade.sql"
	echo "/var/www/data/slant.db: patch aborted"
	exit 1
fi

sqlite3 "/var/www/data/slant.db" < "/var/www/data/slant-upgrade.sql"
install -m 0444  "@SHAREDIR@/slant/slant.kwbp" "/var/www/data/slant.kwbp"
rm -f "/var/www/data/slant-upgrade.sql"
