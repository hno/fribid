#
#  Copyright (c) 2009-2011 Samuel Lidén Borell <samuel@kodafritt.se>
# 
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#  
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#  
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.
#

CFLAGS ?= -O2 -g
COMMONCFLAGS=$(CFLAGS) -Wall -Wextra -std=c99 -pedantic -Wno-unused-parameter
PKG_DEPS=$(if $(WITH_GTK2),'gtk+-2.0 >= 2.18' gdk-2.0) $(if $(WITH_GTK3),gtk+-3.0) glib-2.0 gthread-2.0 $(if $(ENABLE_PKCS11),libp11) libcrypto
CCFLAGS=$(COMMONCFLAGS) -I../npapi/include `pkg-config --cflags $(PKG_DEPS)` -DGTK_DISABLE_DEPRECATED=1 -DGDK_DISABLE_DEPRECATED=1 -DG_DISABLE_DEPRECATED=1 -DGSEAL_ENABLE -DFRIBID_CLIENT
# You may have to add -lpthread after $(LDFLAGS) on OpenBSD
LINKFLAGS=$(CFLAGS) $(LDFLAGS) -Wl,--as-needed
LIBS=`pkg-config --libs $(PKG_DEPS)`

# Files to be installed
LIBEXEC_PATH=`../configure --internal--get-define=LIBEXEC_PATH`
SHARE_PATH=`../configure --internal--get-define=SHARE_PATH`
UI_PATH=`../configure --internal--get-define=UI_PATH`
SIGNING_EXECUTABLE=`../configure --internal--get-define=SIGNING_EXECUTABLE`
UI_GTK_XML=`../configure --internal--get-define=UI_GTK_XML`
ENABLE_PKCS11=$(shell ../configure --internal--get-define=ENABLE_PKCS11|grep 1)
WITH_GTK2=$(shell ../configure --internal--get-define=WITH_GTK2|grep 1)
WITH_GTK3=$(shell ../configure --internal--get-define=WITH_GTK3|grep 1)

OBJECTS=backend.o bankid.o certutil.o $(if $(ENABLE_PKCS11),pkcs11.o) pkcs12.o request.o main.o misc.o pipe.o posix.o prefs.o glibconfig.o gtk.o xmldsig.o secmem.o

all: sign gtk/sign.xml

backend.o: ../common/biderror.h ../common/bidtypes.h backend.h backend_private.h
bankid.o: ../common/biderror.h ../common/bidtypes.h bankid.h backend.h misc.h platform.h prefs.h xmldsig.h
certutil.o: certutil.h misc.h platform.h
glibconfig.o: platform.h misc.h
gtk.o: ../common/biderror.h ../common/bidtypes.h backend.h bankid.h certutil.h platform.h misc.h
main.o: ../common/biderror.h ../common/bidtypes.h ../common/pipe.h backend.h bankid.h misc.h platform.h prefs.h secmem.h
misc.o: misc.h
pkcs11.o: ../common/biderror.h ../common/bidtypes.h backend.h backend_private.h prefs.h misc.h
pkcs12.o: ../common/biderror.h ../common/bidtypes.h backend.h backend_private.h certutil.h misc.h request.h
pipe.o: ../common/pipe.h ../common/pipe.c
posix.o: platform.h
prefs.o: prefs.h platform.h
request.o: request.h
xmldsig.o: xmldsig.h backend.h certutil.h misc.h
secmem.o: secmem.h

.c.o:
	$(CC) $(CCFLAGS) -c $< -o $@

sign: $(OBJECTS)
	$(CC) $(LINKFLAGS) $(OBJECTS) $(LIBS) -o $@

.PHONY: all clean install uninstall
clean:
	rm -f $(OBJECTS) sign

install: all
	install -d $(DESTDIR)$(LIBEXEC_PATH)
	install -d $(DESTDIR)$(UI_PATH)
	install sign $(DESTDIR)$(LIBEXEC_PATH)
	install -m 644 gtk/sign.xml $(DESTDIR)$(UI_PATH)

uninstall:
	rm -f $(DESTDIR)$(SIGNING_EXECUTABLE) $(DESTDIR)$(UI_GTK_XML)
	[ ! -d $(DESTDIR)$(LIBEXEC_PATH) ] || rmdir $(DESTDIR)$(LIBEXEC_PATH) 2> /dev/null || true
	[ ! -d $(DESTDIR)$(UI_PATH) ] || rmdir $(DESTDIR)$(UI_PATH)
	[ ! -d $(DESTDIR)$(SHARE_PATH) ] || rmdir $(DESTDIR)$(SHARE_PATH)

$(OBJECTS): ../common/defines.h ../common/config.h
../common/config.h:
	@echo "You must run ./configure first." >&2 && false
../common/defines.h:

