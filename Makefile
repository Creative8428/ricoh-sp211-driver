# Native (Apple Silicon + Intel) CUPS filter for Ricoh SP 211 / SP 221 class printers
JBIG    = third_party/jbigkit-2.2/libjbig
CFLAGS  = -O2 -Wall -Wno-deprecated-declarations -I$(JBIG) -arch arm64 -arch x86_64 -mmacosx-version-min=12.0
LDLIBS  = -lcups

build/rastertosp211: src/rastertosp211.c src/dither_matrix.h $(JBIG)/jbig.c $(JBIG)/jbig_ar.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/rastertosp211.c $(JBIG)/jbig.c $(JBIG)/jbig_ar.c $(LDLIBS)

clean:
	rm -rf build dist

dmg:
	./packaging/build.sh
