
all: libdisk libfs
	gcc -Wall -L. main.c -lfs -ldisk -o main

libfs:
	gcc -Wall -fPIC -shared -Wl,-soname,libfs.so.1 -o libfs.so.1.0 fs.c
	ln -sf libfs.so.1.0 libfs.so.1
	ln -sf libfs.so.1.0 libfs.so
	
libdisk:
	gcc -Wall -fPIC -shared -Wl,-soname,libdisk.so.1 -o libdisk.so.1.0 disk.c
	ln -sf libdisk.so.1.0 libdisk.so.1
	ln -sf libdisk.so.1.0 libdisk.so

clean:
	rm -f *.so* main

.PHONY: clean
	
	 
