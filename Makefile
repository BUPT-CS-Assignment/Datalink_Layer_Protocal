CC=gcc
CFLAGS=-O2 -Wall

root : datalink.o protocol.o lprintf.o crc32.o
	gcc datalink.o protocol.o lprintf.o crc32.o -o Datalink -lm
	${RM} *.o

run :
	@./Datalink $(MAINARGS)

clean:
	${RM} *.o datalink *.log
