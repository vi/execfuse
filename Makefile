all: execfuse

CFLAGS=-O2 -ggdb -Wall

execfuse: execfuse.c
		${CC} ${LDFLAGS} ${CFLAGS} $(shell pkg-config fuse --cflags --libs) execfuse.c -o execfuse

static:
		${CC} -static ${LDFLAGS} ${CFLAGS} execfuse.c $(shell pkg-config fuse --cflags --libs) -lpthread -lrt -ldl  -o execfuse-static
		
test:
		bash test.sh
