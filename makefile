all: biboServer biboClient

biboServer: biboServer.o
	gcc biboServer.o -o biboServer -pthread -lrt

biboServer.o: biboServer.c
	gcc -c -ansi -pedantic-errors -Wall *.c -std=gnu99 biboServer.c

biboClient: biboClient.o
	gcc biboClient.o -o biboClient -pthread -lrt

biboClient.o: biboClient.c
	gcc -c -ansi -pedantic-errors -Wall *.c -std=gnu99 biboClient.c

clean:
	rm -f biboServer biboClient *.o
