# Native (Apple Silicon + Intel) CUPS filter and AirPrint server for Ricoh SP 211 / SP 221 class printers
JBIG    = third_party/jbigkit-2.2/libjbig
CORE    = src/lilo2.c $(JBIG)/jbig.c $(JBIG)/jbig_ar.c
CFLAGS  = -O2 -Wall -Wno-deprecated-declarations -Isrc -I$(JBIG) -arch arm64 -arch x86_64 -mmacosx-version-min=12.0
LDLIBS  = -lcups

build/rastertosp211: src/rastertosp211.c src/lilo2.h src/dither_matrix.h $(CORE)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/rastertosp211.c $(CORE) $(LDLIBS)

clean:
	rm -rf build dist

dmg:
	./packaging/build.sh

# AirPrint server: native build (for local testing) and static OpenWrt/aarch64 build (needs zig)
build/sp211-airprintd: src/airprintd.c src/icon_png.h src/lilo2.h $(CORE)
	mkdir -p build
	$(CC) -O2 -Wall -Isrc -I$(JBIG) -o $@ src/airprintd.c $(CORE) -lpthread

ROUTER_TARGET ?= aarch64-linux-musl
build/router/sp211-airprintd: src/airprintd.c src/icon_png.h src/lilo2.h $(CORE)
	mkdir -p build/router
	zig cc -target $(ROUTER_TARGET) -O2 -static -s -Isrc -I$(JBIG) -o $@ src/airprintd.c $(CORE) -lpthread 2>&1 | grep -v NDEBUG | grep -v "define NDEBUG" | grep -v "previous definition" | grep -v "^ *|" | grep -v "^ *^" | grep -v "warning generated" | grep -v "command line" || true

build/router/sp211-airprint-router.tar.gz: build/router/sp211-airprintd packaging/router/setup.sh packaging/router/sp211-airprint.init
	rm -rf build/router/bundle && mkdir -p build/router/bundle
	cp build/router/sp211-airprintd packaging/router/setup.sh packaging/router/sp211-airprint.init build/router/bundle/
	tar -C build/router/bundle -czf $@ .

router: build/router/sp211-airprint-router.tar.gz
.PHONY: router dmg clean
