#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "execute_script.h"


int myread(void* _, char* buf, int len) {
    return read(0, buf, len);
}

int mywrite(void* _, const char* buf, int len) {
    int ret = fwrite(buf, 1, len, stdout);
    fflush(stdout);
    return ret;
}

int main(int argc, char* argv[]) {

    write_t stdout_ = NULL;
    read_t stdin_ = NULL;

    if(!getenv("NO_IN")) {
        //fcntl(0, F_SETFL, O_NONBLOCK);
        stdin_ = &myread;
    }
    if(!getenv("NO_OUT")) {
        stdout_ = &mywrite;
    }

    // dir script_name addargs firstarg in out

    int ret = execute_script(argv[1], argv[2], (const char*const*)argv+3, NULL,
                             stdin_, NULL, stdout_, NULL);

    return ret;
}
