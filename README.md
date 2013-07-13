execfuse is a implement FUSE filesystems using a bunch of scripts.

Consider it as a "shell FUSE binding".

For each FUSE call (except of ones that deals with file descriptors) 
execfuse calls your script. For opening files it provides a bit higher 
level abstraction: "read_file" script is called when file should be read 
and "write_file" is called when file should be saved.

Example:

    $ mkdir -p m
    $ ./execfuse examples/xmp m
    $ ls -l m/bin/sh
    lrwxrwxrwx 1 root root 4 Mar 26 00:26 m/bin/sh -> bash
    (executes "examples/xmp/readlink /bin/bash" for this)
    $ ls m/etc/iproute2/
    ematch_map  group  rt_dsfield  rt_protos  rt_realms  rt_scopes	rt_tables
    (executes "exampels/xmp/readdir /etc/iproute" for this)
    $ m/bin/echo qqq
    qqq
    $ mkdir -p m/tmp/1/2/3
    $ echo 12345 > m/tmp/12345
    (executes "exampels/xmp/write_file /tmp/12345" with content piped to stdin)
    $ rm -Rf m/tmp/1*
    $ fusermount -u m
    
Limitations:

* Each file must fit in memory, can't write/read part of file
* Slow by design
* Limited error handling, especially for writing files
* Modifications to files are visible only after file closing

Filesystem examples:
* `examples/xmp` - try to be fusexmp_fh
* `examples/hello` - very simple demo filesystem
* `examples/video_frames` - extract frames from video as `*.ppm` files and enumerate keyframes (using ffmpeg)
