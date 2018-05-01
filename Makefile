gzip: deflate.c deflate.h gzip.c
	gcc -o gzip deflate.c deflate.h gzip.c -lz

clean:
	rm -f gzip gzip.tar.gz

dist:
	tar -cvf gzip.tar deflate.c deflate.h gzip.c
	./gzip gzip.tar
	rm -f gzip.tar
