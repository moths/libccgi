CFLAGS ?= -Wall -Wextra -O2
CPPFLAGS += -I .
RANLIB ?= ranlib
AR ?= ar
prefix ?= /usr
DESTDIR ?= /

libccgi.a: ccgi.o
	$(AR) r libccgi.a ccgi.o
	$(RANLIB) libccgi.a

ccgi.o: ccgi.c ccgi.h

install: libccgi.a
	install -m 0755 -d $(DESTDIR)$(prefix)/lib
	install -m 0755 -d $(DESTDIR)$(prefix)/include
	install -m 0644 libccgi.a $(DESTDIR)$(prefix)/lib
	install -m 0644 ccgi.h $(DESTDIR)$(prefix)/include

clean:
	rm -f *.o *.a
