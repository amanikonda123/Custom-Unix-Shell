CC = gcc
CFLAGS = -Wall -Werror -g -pedantic -std=c99 -std=gnu99

all: mush2

mush2: mush2.o plumbing.o
	$(CC) -o mush2 $(CFLAGS) -L ~pn-cs357/Given/Mush/lib64 mush2.o plumbing.o -lmush

mush2.o: mush2.c mush2.h
	$(CC) $(CFLAGS) -c -I ~pn-cs357/Given/Mush/include -o mush2.o mush2.c

plumbing.o: plumbing.c plumbing.h
	$(CC) $(CFLAGS) -c -I ~pn-cs357/Given/Mush/include -o plumbing.o plumbing.c  

clean: mush2
	rm -f *.o *~
