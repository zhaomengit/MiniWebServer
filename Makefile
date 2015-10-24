all: httpd

httpd: httpd.c
	gcc -W -Wall -g -pthread -o httpd httpd.c

clean:
	rm httpd
