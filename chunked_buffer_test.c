#include <stdio.h>
#include <assert.h>
#include "chunked_buffer.c"

void print_chunked_buffer(struct chunked_buffer *cb) {
    char buf[1000];
    int i;
    int len = chunked_buffer_getlen(cb);
    int ret = chunked_buffer_read(cb, buf, len, 0);
    assert(ret==len);
    printf("A len=%d ", len);
    for (i=0; i<len; ++i) {
        if(buf[i]==0) putchar('_');
        else if (buf[i]>='A' && buf[i]<='Z') putchar(buf[i]); 
        else putchar('?'); 
    }
    printf("\n");
    fflush(stdout);
}

void print_chunked_buffer1(struct chunked_buffer *cb) {
    char buf[1000];
    int i;
    int len = chunked_buffer_getlen(cb);
    printf("1 len=%d ", len);
    for (i=0; i<len; ++i) {
        int ret = chunked_buffer_read(cb, buf+i, 1, i);
        assert(ret==1);
        if(buf[i]==0) putchar('_');
        else if (buf[i]>='A' && buf[i]<='Z') putchar(buf[i]); 
        else putchar('?'); 
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    struct chunked_buffer *cb = chunked_buffer_new(3);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_write(cb, "Q", 1, 0);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_write(cb, "Q", 1, 2);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_write(cb, "Q", 1, 7);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_write(cb, "WWWWW", 5, 2);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_write(cb, "EE", 2, 4);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_truncate(cb, 14);
    print_chunked_buffer(cb); print_chunked_buffer1(cb);
    
    chunked_buffer_delete(cb);
}
