dist/libroaring.so: src/libsqlite3roaring.c
	gcc -O3 -shared -fPIC -o $@ $<
