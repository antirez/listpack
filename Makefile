DEBUG?= -g -ggdb
CFLAGS?= -O2 -Wall -W -std=c99
LDFLAGS= -lm

# Uncomment the following two lines for coverage testing
#
# CFLAGS+=-fprofile-arcs -ftest-coverage
# LDFLAGS+=-lgcov

all: listpack-test

listpack.o: listpack.h
listpack-test.o: listpack.h listpack.c

listpack-test: listpack-test.o listpack.o
	$(CC) -o $@ $^ $(LDFLAGS) $(DEBUG)

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $<

clean:
	rm -f listpack-test *.gcda *.gcov *.gcno *.o
