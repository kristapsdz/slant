.SUFFIXES: .xml .html .8 .8.html .1 .1.html .dot .svg .ts .js

PREFIX	   = /usr/local
WPREFIX	   = /var/www

CFLAGS	  += -g -W -Wall -Wextra -Wmissing-prototypes
CFLAGS	  += -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter

BINDIR     = $(PREFIX)/bin
SBINDIR    = $(PREFIX)/sbin
MANDIR	   = $(PREFIX)/man
SHAREDIR   = $(PREFIX)/share
CGIBIN	   = $(WPREFIX)/cgi-bin
DATADIR	   = $(WPREFIX)/data

DBFILE	   = /data/slant.db
WWWDIR	   = /var/www/vhosts/kristaps.bsd.lv/htdocs/slant

sinclude Makefile.local

VERSION	   = 0.0.7
CPPFLAGS   += -DVERSION=\"$(VERSION)\"

WWW	   = index.html \
	     index.js \
	     index1.svg \
	     slant.1.html \
	     slant-cgi.8.html \
	     slant-collectd.8.html 
DOTAR	   = Makefile \
	     slant-cgi.c \
	     slant-cgi.8 \
	     slant-collectd-openbsd.c \
	     slant-collectd.8 \
	     slant-collectd.c \
	     slant-collectd.h \
	     slant-config.c \
	     slant-dns.c \
	     slant-draw.c \
	     slant-http.c \
	     slant-json.c \
	     slant-upgrade.in.sh \
	     slant-upgrade.8 \
	     slant.1 \
	     slant.c \
	     slant.h \
	     slant.kwbp
SLANT_OBJS = slant.o \
	     slant-config.o \
	     slant-dns.o \
	     slant-draw.o \
	     slant-http.o \
	     slant-json.o \
	     json.o

all: slant.db slant-collectd slant-cgi slant slant-upgrade

www: slant.tar.gz $(WWW)

installwww: www
	mkdir -p $(WWWDIR)
	mkdir -p $(WWWDIR)/snapshots
	install -m 0444 slant.tar.gz $(WWWDIR)/snapshots/slant-$(VERSION).tar.gz
	install -m 0444 slant.tar.gz $(WWWDIR)/snapshots

slant.tar.gz: $(DOTAR)
	mkdir -p .dist/slant-$(VERSION)/
	install -m 0644 $(DOTAR) .dist/slant-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

install: slant-collectd slant-cgi slant slant-upgrade
	mkdir -p $(DESTDIR)$(SHAREDIR)/slant
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(CGIBIN)
	install -m 0444 slant.kwbp $(DESTDIR)$(SHAREDIR)/slant
	install -m 0555 slant-cgi $(DESTDIR)$(CGIBIN)
	install -m 0555 slant-collectd slant-upgrade $(DESTDIR)$(SBINDIR)
	install -m 0555 slant $(DESTDIR)$(BINDIR)
	install -m 0444 slant.1 $(DESTDIR)$(MANDIR)/man1
	install -m 0444 slant-cgi.8 slant-collectd.8 slant-upgrade.8 $(DESTDIR)$(MANDIR)/man8

uninstall:
	rm -f $(DESTDIR)$(SHAREDIR)/slant/slant.kwbp
	rmdir $(DESTDIR)$(SHAREDIR)/slant
	rm -f $(DESTDIR)$(CGIBIN)/slant-cgi
	rm -f $(DESTDIR)$(SBINDIR)/slant-collectd
	rm -f $(DESTDIR)$(SBINDIR)/slant-upgrade
	rm -f $(DESTDIR)$(BINDIR)/slant
	rm -f $(DESTDIR)$(MANDIR)/man1/slant.1
	rm -f $(DESTDIR)$(MANDIR)/man8/slant-cgi.8
	rm -f $(DESTDIR)$(MANDIR)/man8/slant-collectd.8
	rm -f $(DESTDIR)$(MANDIR)/man8/slant-upgrade.8

slant-upgrade: slant-upgrade.in.sh
	sed -e "s!@DATADIR@!$(DATADIR)!g" \
	    -e "s!@CGIBIN@!$(CGIBIN)!g" \
	    -e "s!@SHAREDIR@!$(SHAREDIR)!g" slant-upgrade.in.sh >$@

# Only run this for development.
# Real systems will install with slant-upgrade(8).

installdb: slant.db
	mkdir -p $(DESTDIR)$(DATADIR)
	install -m 0666 slant.db $(DESTDIR)$(DATADIR)
	install -m 0444 slant.kwbp $(DESTDIR)$(DATADIR)

slant-collectd: slant-collectd.o slant-collectd-openbsd.o db.o
	$(CC) -o $@ $(LDFLAGS) slant-collectd.o db.o slant-collectd-openbsd.o -lksql -lsqlite3 

config.h:
	echo "#define DBFILE \"$(DBFILE)\"" > config.h

slant-cgi: slant-cgi.o db.o json.o
	$(CC) -static -o $@ $(LDFLAGS) slant-cgi.o db.o json.o -lkcgi -lkcgijson -lz -lksql -lsqlite3 -lm -lpthread

slant-cgi.o: config.h

slant: $(SLANT_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(SLANT_OBJS) -ltls -lncurses -lkcgijson -lkcgi -lz

clean:
	rm -f slant.db slant.sql slant.tar.gz slant-upgrade
	rm -f db.o db.c db.h json.c json.o json.h extern.h config.h
	rm -f slant-collectd slant-collectd.o slant-collectd-openbsd.o
	rm -f slant-cgi slant-cgi.o
	rm -f slant $(SLANT_OBJS)
	rm -f $(WWW)

slant.db: slant.sql
	rm -f $@
	sqlite3 $@ < slant.sql

slant.sql: slant.kwbp
	kwebapp-sql slant.kwbp > $@

slant-collectd-openbsd.o slant-collectd.o: slant-collectd.h

db.o slant-collectd.o slant-cgi.o: db.h

json.o slant-cgi.o slant-json.o slant.o: json.h

$(SLANT_OBJS): slant.h

db.h: extern.h

json.h: extern.h

slant.h: extern.h

extern.h: slant.kwbp
	kwebapp-c-header -s -g EXTERN_H -Nd slant.kwbp > $@

db.h: slant.kwbp
	kwebapp-c-header -s -Nb slant.kwbp > $@

json.h: slant.kwbp
	kwebapp-c-header -s -g JSON_H -jJ -Nbd slant.kwbp > $@

db.c: slant.kwbp
	kwebapp-c-source -s -h extern.h,db.h slant.kwbp > $@

json.c: slant.kwbp
	kwebapp-c-source -s -Ij -jJ -Nd -h extern.h,json.h slant.kwbp > $@

.xml.html:
	cp -f $< $@

.8.8.html .1.1.html:
	mandoc -Thtml $< >$@

.dot.svg:
	dot -Tsvg $< | xsltproc --novalid notugly.xsl - >$@

.ts.js:
	tsc --strict --alwaysStrict --removeComments --outFile $@ $<
