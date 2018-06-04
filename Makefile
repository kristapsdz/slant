CFLAGS	  += -g -W -Wall -Wextra -Wmissing-prototypes
CFLAGS	  += -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter
CPPFLAGS   = -I/usr/local/opt/include -I/usr/local/include
LDFLAGS	   = -L/usr/local/opt/lib -L/usr/local/lib
BINDIR     = /usr/local/bin
MANDIR	   = /usr/local/man
CGIBIN	   = /var/www/cgi-bin
DATADIR	   = /var/www/data
DBFILE	   = /data/slant.db

sinclude Makefile.local

SLANT_OBJS = slant.o slant-http.o slant-dns.o slant-json.o slant-jsonobj.o slant-draw.o

all: slant.db slant-collectd slant-cgi slant

server: slant.db slant-collectd slant-cgi

client: slant

installserver: installcgi installdb installdaemon

installdaemon: slant-collectd
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0555 slant-collectd $(DESTDIR)$(BINDIR)
	install -m 0444 slant-collectd.1 $(DESTDIR)$(MANDIR)/man1

installcgi: slant-cgi
	mkdir -p $(DESTDIR)$(CGIBIN)
	install -m 0555 slant-cgi $(DESTDIR)$(CGIBIN)

installdb: slant.db
	mkdir -p $(DESTDIR)$(DATADIR)
	install -m 0666 slant.db $(DESTDIR)$(DATADIR)

slant-collectd: slant-collectd.o slant-collectd-openbsd.o db.o
	$(CC) -o $@ $(LDFLAGS) slant-collectd.o db.o slant-collectd-openbsd.o -lksql -lsqlite3 

config.h:
	echo "#define DBFILE \"$(DBFILE)\"" > config.h

slant-cgi: slant-cgi.o db.o json.o
	$(CC) -static -o $@ $(LDFLAGS) slant-cgi.o db.o json.o -lkcgi -lkcgijson -lz -lksql -lsqlite3 -lm -lpthread

slant-cgi.o: config.h

slant: $(SLANT_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(SLANT_OBJS) -ltls -lncurses

clean:
	rm -f slant.db slant.sql 
	rm -f db.o db.c db.h json.c json.o json.h extern.h config.h
	rm -f slant-collectd slant-collectd.o slant-collectd-openbsd.o
	rm -f slant-cgi slant-cgi.o
	rm -f slant $(SLANT_OBJS)

slant.db: slant.sql
	rm -f $@
	sqlite3 $@ < slant.sql

slant.sql: slant.kwbp
	kwebapp-sql slant.kwbp > $@

slant-collectd-openbsd.o slant-collectd.o: slant-collectd.h

db.o slant-collectd.o slant-cgi.o: db.h

json.o slant-cgi.o: json.h

$(SLANT_OBJS): slant.h

db.h: extern.h

json.h: extern.h

slant.h: extern.h

extern.h: slant.kwbp
	kwebapp-c-header -s -g EXTERN_H -Nd slant.kwbp > $@

db.h: slant.kwbp
	kwebapp-c-header -s -Nb slant.kwbp > $@

json.h: slant.kwbp
	kwebapp-c-header -s -g JSON_H -j -Nbd slant.kwbp > $@

db.c: slant.kwbp
	kwebapp-c-source -s -h extern.h,db.h slant.kwbp > $@

json.c: slant.kwbp
	kwebapp-c-source -s -Idj -j -Nd -h extern.h,json.h slant.kwbp > $@
