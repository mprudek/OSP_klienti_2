all: httpd

httpd: httpd.c
	g++ -std=c++11 -Ofast -march=westmere -mtune=westmere -W -Wall -o httpd httpd.c -lz -pthread -static -fpermissive

c: httpd.c
	gcc -std=gnu11 -Ofast -march=westmere -mtune=westmere -W -Wall -o httpd httpd.c -lz -pthread -static

clean:
	rm httpd
