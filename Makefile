gzip: deflate.c deflate.h gzip.c
	gcc -o gzip deflate.c gzip.c utils.c inflate.c -lz -lpthread

clean:
	rm -f gzip gzip.tar.gz

dist:
	tar -cvf gzip.tar deflate.c deflate.h gzip.c utils.c utils.h inflate.c inflate.h
	./gzip gzip.tar
	rm -f gzip.tar
