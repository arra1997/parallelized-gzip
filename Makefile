gzip: deflate.c deflate.h gzip.c
	gcc -o gzip deflate.c deflate.h gzip.c utils.c utils.h -lz

clean:
	rm -f gzip gzip.tar.gz

dist:
	tar -cvf gzip.tar deflate.c deflate.h gzip.c utils.c utils.h
	./gzip gzip.tar
	rm -f gzip.tar
