.PHONY: clean all

all: libmuxer.a

muxer.o: *.c *.h
	gcc -g -c muxer.c -o muxer.o -I/usr/local/include

libmuxer.a: muxer.o
	ar cr libmuxer.a muxer.o

clean:
	rm libmuxer.a muxer.o
