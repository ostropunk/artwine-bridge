VERSION   := 0.1.0
PREFIX    ?= /usr
LIBDIR    ?= $(PREFIX)/lib/artwine-bridge
BINDIR    ?= $(PREFIX)/bin
UDEVDIR   ?= $(PREFIX)/lib/udev/rules.d
DOCDIR    ?= $(PREFIX)/share/doc/artwine-bridge
WINELIB   ?= /usr/lib/wine/x86_64-windows
MINGW     ?= x86_64-w64-mingw32-gcc
CFLAGS    ?= -O2
CFLAGS    += -Wall -Wextra
MINGW_CFLAGS ?= -O2 -Wall -Wextra
B         := build

all: $(B)/libusb.dll $(B)/artwine.so

$(B):
	mkdir -p $@

$(B)/libusb.dll: src/pe.c src/common.h | $(B)
	$(MINGW) $(MINGW_CFLAGS) -shared -o $@ src/pe.c $(WINELIB)/libwinecrt0.a $(WINELIB)/libntdll.a -lkernel32

$(B)/artwine.so: src/unix.c src/common.h | $(B)
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o $@ src/unix.c $$(pkg-config --cflags --libs libusb-1.0) -lpthread

$(B)/usbtest.exe: test/usbtest.c | $(B)
	$(MINGW) $(MINGW_CFLAGS) -Wno-cast-function-type -o $@ test/usbtest.c

test: all $(B)/usbtest.exe
	ARTWINE_LOG=$(CURDIR)/$(B)/test.log wine $(B)/usbtest.exe 'Z:$(subst /,\,$(CURDIR)/$(B)/libusb.dll)'

install: all
	install -Dm644 $(B)/libusb.dll  $(DESTDIR)$(LIBDIR)/libusb.dll
	install -Dm755 $(B)/artwine.so  $(DESTDIR)$(LIBDIR)/artwine.so
	install -Dm755 artwine-bridge   $(DESTDIR)$(BINDIR)/artwine-bridge
	install -Dm644 udev/70-artwine-arturia.rules $(DESTDIR)$(UDEVDIR)/70-artwine-arturia.rules
	install -Dm644 README.md        $(DESTDIR)$(DOCDIR)/README.md

clean:
	rm -rf $(B)

.PHONY: all test install clean
