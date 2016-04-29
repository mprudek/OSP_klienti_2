all: httpd

httpd: httpd.c
	g++ -std=c++11 -Ofast -march=westmere -mtune=westmere -W -Wall -lpthread -o httpd httpd.c -lz -pthread -fpermissive -static

clean:
	rm httpd
