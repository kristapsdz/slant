.SUFFIXES: .8 .8.html .1 .1.html .dot .svg .ts .js

include Makefile.configure

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

# Additional libraries required per component.
# These should be set in Makefile.local.

LDADD_SLANT_COLLECTD =
LDADD_SLANT_CGI =
LDADD_SLANT =

sinclude Makefile.local

VERSION	   = 0.0.21
CPPFLAGS   += -DVERSION=\"$(VERSION)\"

WWW	   = index.html \
	     index.js \
	     index1.svg \
	     slant.1.html \
	     slant-cgi.8.html \
	     slant-collectd.8.html 
DOTAR	   = compats.c \
	     tests.c \
	     Makefile \
	     slant-cgi.c \
	     slant-cgi.8 \
	     slant-collectd-freebsd.c \
	     slant-collectd-linux.c \
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
SLANT_COLLECTD_OBJS = \
	     compats.o \
	     db.o \
	     slant-collectd.o \
	     slant-collectd-freebsd.o \
	     slant-collectd-linux.o \
	     slant-collectd-openbsd.o
OBJS	   = $(SLANT_OBJS) \
	     slant-cgi.o \
	     slant-collectd.o \
	     slant-collectd-freebsd.o \
	     slant-collectd-linux.o \
	     slant-collectd-openbsd.o

# Needed on FreeBSD.
CFLAGS += $(CPPFLAGS)

all: slant.db slant-collectd slant-cgi slant slant-upgrade

www: slant.tar.gz $(WWW)

installwww: www
	mkdir -p $(WWWDIR)
	mkdir -p $(WWWDIR)/snapshots
	$(INSTALL_DATA) slant.tar.gz $(WWWDIR)/snapshots/slant-$(VERSION).tar.gz
	$(INSTALL_DATA) slant.tar.gz $(WWWDIR)/snapshots
	$(INSTALL_DATA) $(WWW) index.css screen1.jpg screen2.jpg screen3.jpg $(WWWDIR)

slant.tar.gz: $(DOTAR) configure
	mkdir -p .dist/slant-$(VERSION)/
	install -m 0777 configure .dist/slant-$(VERSION)
	install -m 0644 $(DOTAR) .dist/slant-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

install: slant-collectd slant-cgi slant slant-upgrade
	mkdir -p $(DESTDIR)$(SHAREDIR)/slant
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(CGIBIN)
	$(INSTALL_DATA) slant.kwbp $(DESTDIR)$(SHAREDIR)/slant
	$(INSTALL_PROGRAM) slant-cgi $(DESTDIR)$(CGIBIN)
	$(INSTALL_PROGRAM) slant-collectd slant-upgrade $(DESTDIR)$(SBINDIR)
	$(INSTALL_PROGRAM) slant $(DESTDIR)$(BINDIR)
	$(INSTALL_MAN) slant.1 $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_MAN) slant-cgi.8 slant-collectd.8 slant-upgrade.8 $(DESTDIR)$(MANDIR)/man8

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

slant-collectd: $(SLANT_COLLECTD_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(SLANT_COLLECTD_OBJS) -lksql -lsqlite3 $(LDADD_SLANT_COLLECTD)

params.h:
	echo "#define DBFILE \"$(DBFILE)\"" > params.h

slant-cgi: slant-cgi.o db.o json.o compats.o
	$(CC) -static -o $@ $(LDFLAGS) slant-cgi.o db.o json.o compats.o -lkcgi -lkcgijson -lz -lksql -lsqlite3 -lm -lpthread $(LDADD_SLANT_CGI)

slant-cgi.o: params.h

slant: $(SLANT_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(SLANT_OBJS) -ltls -lncurses -lkcgijson -lkcgi -lz $(LDADD_SLANT)

clean:
	rm -f slant.db slant.sql slant.tar.gz slant-upgrade
	rm -f db.c db.h json.c json.h extern.h params.h
	rm -f slant-collectd slant-cgi slant
	rm -f $(OBJS) compats.o db.o json.o
	rm -f $(WWW)

distclean: clean
	rm -f Makefile.configure config.h config.log

slant.db: slant.sql
	rm -f $@
	sqlite3 $@ < slant.sql

slant.sql: slant.kwbp
	ort-sql slant.kwbp > $@

slant-collectd-openbsd.o slant-collectd-linux.o slant-collectd.o: slant-collectd.h

db.o slant-collectd.o slant-cgi.o: db.h

json.o slant-cgi.o slant-json.o slant.o: json.h

$(OBJS): config.h

$(SLANT_OBJS): slant.h

db.h: extern.h

json.h: extern.h

slant.h: extern.h

extern.h: slant.kwbp
	ort-c-header -s -g EXTERN_H -Nd slant.kwbp > $@

db.h: slant.kwbp
	ort-c-header -s -Nb slant.kwbp > $@

json.h: slant.kwbp
	ort-c-header -s -g JSON_H -jJ -Nbd slant.kwbp > $@

db.c: slant.kwbp
	ort-c-source -s -h extern.h,db.h slant.kwbp > $@

json.c: slant.kwbp
	ort-c-source -s -Ij -jJ -Nd -h extern.h,json.h slant.kwbp > $@

index.html: index.xml versions.xml
	sblg -o $@ -t index.xml versions.xml

.8.8.html .1.1.html:
	mandoc -Thtml $< >$@

.dot.svg:
	dot -Tsvg $< | xsltproc --novalid notugly.xsl - >$@

.ts.js:
	tsc --strict --alwaysStrict --removeComments --outFile $@ $<
