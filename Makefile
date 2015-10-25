all: httpd

httpd: httpd.c
	gcc -W -g -Wall -g -pthread -o httpd httpd.c

clean:
	rm httpd
