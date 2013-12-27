OPTIMIZE=-g -Wall
#OPTIMIZE=-O2 

GTKINC=$(shell pkg-config --cflags gtk+-2.0) -DG_DISABLE_DEPRECATED  -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED
GTKLIBS=$(shell pkg-config --libs gtk+-2.0)
GLIBLIBS=$(shell pkg-config --libs glib-2.0)

LIBS = -lm

PREFIX=/usr/local
DATADIR=$(PREFIX)/share

# location in which binaries are installed
BINDIR=$(PREFIX)/bin
# location in which data files will be installed
LIBDIR=$(DATADIR)/kanjipad

#
# On Win32, uncomment the following to avoid getting console windows
#
#LDFLAGS=-mwindows

INSTALL=install

####### No editing should be needed below here ##########

PACKAGE = kanjipad
VERSION = 2.0.0

OBJS = kpengine.o scoring.o util.o
CFLAGS = $(OPTIMIZE) $(GTKINC) -DFOR_PILOT_COMPAT -DKP_LIBDIR=\"$(LIBDIR)\" -DBINDIR=\"$(BINDIR)\"

all: kpengine kanjipad jdata.dat

scoring.o: jstroke/scoring.c
	$(CC) -c -o scoring.o $(CFLAGS) $(LIBS) -Ijstroke jstroke/scoring.c 

util.o: jstroke/util.c
	$(CC) -c -o util.o $(CFLAGS) $(LIBS) -Ijstroke jstroke/util.c

kpengine: $(OBJS)
	$(CC) -o kpengine $(OBJS) $(GLIBLIBS) $(LDFLAGS) $(GTKLIBS)

kanjipad: kanjipad.o padarea.o
	$(CC) -o kanjipad kanjipad.o padarea.o $(GTKLIBS) $(LDFLAGS) $(LIBS)

jdata.dat: jstroke/strokedata.h conv_jdata.pl
	perl conv_jdata.pl < jstroke/strokedata.h > jdata.dat

install: kanjipad kpengine jdata.dat
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 kanjipad $(DESTDIR)$(BINDIR)/kanjipad
	install -m 0755 kpengine $(DESTDIR)$(BINDIR)/kpengine
	install -d $(DESTDIR)$(LIBDIR)
	install -m 0644 jdata.dat $(DESTDIR)$(LIBDIR)/jdata.dat

clean:
	rm -rf *.o jdata.dat kpengine kanjipad

$(PACKAGE).spec: $(PACKAGE).spec.in
	( sed s/@VERSION@/$(VERSION)/ < $< > $@.tmp && mv $@.tmp $@ ) || ( rm $@.tmp && false )

dist: $(PACKAGE).spec
	distdir=$(PACKAGE)-$(VERSION) ;					\
	tag=`echo $$distdir | tr a-z.- A-Z__` ;				\
	cvs tag -F $$tag &&						\
	cvs export -r $$tag -d $$distdir $(PACKAGE) &&			\
	cp $(PACKAGE).spec $$distdir &&					\
	tar cvf - $$distdir | gzip -c --best > $$distdir.tar.gz &&	\
	rm -rf $$distdir

distcheck: dist
	distdir=$(PACKAGE)-$(VERSION) ;		\
	tar xvfz $$distdir.tar.gz &&		\
	cd $$distdir &&				\
	make &&					\
	cd .. &&				\
	rm -rf $$distdir

.PHONY: dist distcheck
