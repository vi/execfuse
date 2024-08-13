all: execfuse

CFLAGS=-O2 -ggdb -Wall

FILES=execfuse.c chunked_buffer.c execute_script.c
HEADERS=chunked_buffer.h execute_script.h

execfuse: ${FILES} ${HEADERS}
		${CC} ${LDFLAGS} ${CFLAGS} ${FILES} $(shell pkg-config fuse --cflags --libs) -o execfuse

execfuse-static: ${FILES} ${HEADERS}
		${CC} -static ${LDFLAGS} ${CFLAGS} ${FILES} $(shell pkg-config fuse --cflags --libs) -lpthread -lrt -ldl  -o execfuse-static

test:
		bash tests.sh
