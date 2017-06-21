LIBDIR=$(DESTDIR)/usr/lib
INCDIR=$(DESTDIR)/usr/include
MANDIR=$(DESTDIR)/usr/share/man/man2

PATH=/usr/i386-linux-cgc/bin:/bin:/usr/bin

.SUFFIXES: .md .2.gz

C_SRC   = $(wildcard *.c)
ASM_SRC = $(wildcard *.s)
OBJS    = $(C_SRC:%.c=%.o)
OBJS   += $(ASM_SRC:%.s=%.o)

ASFLAGS=

%.2.gz: %.md
	pandoc -s -t man $< | gzip -9 > $@

all: libcgc.a \
	allocate.2.gz deallocate.2.gz fdwait.2.gz random.2.gz receive.2.gz \
	_terminate.2.gz transmit.2.gz cgcabi.2.gz

libcgc.a: $(OBJS)
	$(AR) cruv $@ $(OBJS)


%.o:%.s
	$(AS) -o $@ $< $(ASFLAGS)

%.o:%.c
	$(CC) -c -g -o $@ $< $(CFLAGS)

install: libcgc.a
	install -d $(LIBDIR)
	install -d $(INCDIR)
	install libcgc.a $(LIBDIR)
	install libcgc.h $(INCDIR)
	install -d $(MANDIR)
	install -m 444 allocate.2.gz $(MANDIR)
	install -m 444 cgcabi.2.gz $(MANDIR)
	install -m 444 deallocate.2.gz $(MANDIR)
	install -m 444 fdwait.2.gz $(MANDIR)
	install -m 444 random.2.gz $(MANDIR)
	install -m 444 receive.2.gz $(MANDIR)
	install -m 444 _terminate.2.gz $(MANDIR)
	install -m 444 transmit.2.gz $(MANDIR)

clean:
	rm -f libcgc.[oa] *.2.gz $(OBJS)
