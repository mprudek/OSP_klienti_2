all: httpd

httpd: httpd.c
	g++ -std=c++11 -Ofast -W -Wall -lpthread -o httpd httpd.c -lz -pthread -fpermissive -static

clean:
	rm httpd
