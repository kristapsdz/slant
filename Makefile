CFLAGS	  += -O0 -g -W -Wall -Wextra -Wmissing-prototypes
CFLAGS	  += -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter
CPPFLAGS   = -I/usr/local/opt/include -I/usr/local/include
LDFLAGS	   = -L/usr/local/opt/lib -L/usr/local/lib

all: slant.db slant-collectd slant-cgi slant

slant-collectd: slant-collectd.o slant-collectd-openbsd.o db.o
	$(CC) -o $@ $(LDFLAGS) slant-collectd.o db.o slant-collectd-openbsd.o -lksql -lsqlite3 

slant-cgi: slant-cgi.o db.o json.o
	$(CC) -static -o $@ $(LDFLAGS) slant-cgi.o db.o json.o -lkcgi -lkcgijson -lz -lksql -lsqlite3 -lm -lpthread

slant: slant.o slant-http.o slant-dns.o slant-json.o slant-jsonobj.o slant-draw.o
	$(CC) -o $@ $(LDFLAGS) slant.o slant-http.o slant-dns.o slant-json.o slant-jsonobj.o slant-draw.o 

clean:
	rm -f slant.db slant.sql 
	rm -f db.o db.c db.h json.c json.o json.h extern.h
	rm -f slant-collectd slant-collectd.o slant-collectd-openbsd.o
	rm -f slant-cgi slant-cgi.o
	rm -f slant slant.o slant-http.o slant-dns.o slant-json.o slant-jsonobj.o slant-draw.o

slant.db: slant.sql
	rm -f $@
	sqlite3 $@ < slant.sql

slant.sql: slant.kwbp
	kwebapp-sql slant.kwbp > $@

slant-collectd-openbsd.o slant-collectd.o: slant-collectd.h

db.o slant-collectd.o slant-cgi.o: db.h

json.o slant-cgi.o: json.h

slant.o slant-dns.o slant-http.o slant-json.o slant-jsonobj.o: slant.h

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
