
#define FUSE_USE_VERSION 26

#define _GNU_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <semaphore.h>

#include "common.h"
#include "chunked_buffer.h"
#include "execute_script.h"

#define SCRIPT_API_VERSION "0"

#define STATIC_MODE_STRING(bits, var) \
 char var[16];\
 bits&=07777;\
 sprintf(var, "0%04o", bits)

typedef int bool;
#define TRUE 1
#define FALSE 0

char working_directory[EXECFUSE_MAX_PATHLEN];
const char*const* addargs;


static int call_script_simple(const char* script_name, const char* param);
static int call_script_simple2(const char* script_name, const char* param1, const char* param2);
static int call_script_simple3(const char* script_name, const char* param1,
                               const char* param2, const char* param3);
static struct chunked_buffer* call_script_stdout(const char* script_name, const char* param);



// "ino=2 mode=drwxr-xr-x nlink=35 uid=0 gid=0 rdev=0 size=1224 blksize=512 blocks=2 atime=1365035428.0000000000 mtime=1368450727.0000000000 ctime=1368450727.0000000000 filename\0"
// find / -maxdepth 0 -printf 'ino=%i mode=%M nlink=%n uid=%U gid=%G rdev=0 size=%s blksize=512 blocks=%b atime=%A@ mtime=%T@ ctime=%C@ %f\0'
    
static int scanstat(const char* str, struct stat *stbuf) {
    char mode_ox, mode_ow, mode_or, mode_gx, mode_gw, mode_gr, mode_ux, mode_uw, mode_ur, mode;
    double atime, mtime, ctime;
    int l;
    
    long long int st_ino;
    long long int nlink;
    long long int rdev;
    long long int size;
    long long int blocks;
    
    int ret;
    ret = sscanf(str, "ino=%lli mode=%c%c%c%c%c%c%c%c%c%c nlink=%lli uid=%i gid=%i "
        "rdev=%lli size=%lli blksize=%li blocks=%lli atime=%lf mtime=%lf ctime=%lf %n"
         ,&st_ino
         ,&mode, &mode_ur, &mode_uw, &mode_ux, &mode_gr, &mode_gw, &mode_gx, &mode_or, &mode_ow, &mode_ox
         ,&nlink
         ,&stbuf->st_uid
         ,&stbuf->st_gid
         ,&rdev
         ,&size
         ,&stbuf->st_blksize
         ,&blocks
         ,&atime, &mtime, &ctime
         ,&l
         );
    
    if(ret!= 21) {
        return 0;
    }
    stbuf->st_ino = st_ino;
    stbuf->st_nlink = nlink;
    stbuf->st_rdev = rdev;
    stbuf->st_size = size;
    stbuf->st_blocks = blocks;
    stbuf->st_ctime = ctime;
    stbuf->st_mtime = mtime;
    stbuf->st_atime = atime;
    stbuf->st_mode = 0
        | ((mode_ox == '-') ? 0 : S_IXOTH)
        | ((mode_ow == '-') ? 0 : S_IWOTH)
        | ((mode_or == '-') ? 0 : S_IROTH)
        | ((mode_gx == '-') ? 0 : S_IXGRP)
        | ((mode_gw == '-') ? 0 : S_IWGRP)
        | ((mode_gr == '-') ? 0 : S_IRGRP)
        | ((mode_ux == '-') ? 0 : S_IXUSR)
        | ((mode_uw == '-') ? 0 : S_IWUSR)
        | ((mode_ur == '-') ? 0 : S_IRUSR)
        ;
    switch(mode) {
        case '-': stbuf->st_mode |= S_IFREG; break;    
        case 'd': stbuf->st_mode |= S_IFDIR; break;    
        case 'p': stbuf->st_mode |= S_IFIFO; break;    
        case 'c': stbuf->st_mode |= S_IFCHR; break;    
        case 'b': stbuf->st_mode |= S_IFBLK; break;    
        case 'l': stbuf->st_mode |= S_IFLNK; break;    
        case 's': stbuf->st_mode |= S_IFSOCK; break;    
    }
    if (mode_ox == 't') stbuf->st_mode |= S_ISVTX;
    if (mode_ux == 's') stbuf->st_mode |= S_ISUID;
    if (mode_gx == 's') stbuf->st_mode |= S_ISGID;
    return l;
}

static int execfuse_getattr(const char *path, struct stat *stbuf)
{
    if(!path) return -ENOSYS;
    
    struct chunked_buffer* r = call_script_stdout("getattr", path);
    if(!r) return -ENOENT;
    
    char buf[EXECFUSE_CHUNKSIZE];
    int ret = chunked_buffer_read(r, buf, EXECFUSE_CHUNKSIZE-1, 0);
    buf[ret]=0;
    chunked_buffer_delete(r);
    
    
    ret = scanstat(buf, stbuf);
    if (!ret) {        
        return -EINVAL;
    }
    
    return 0;
}


static int execfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi) {

    (void) offset;
    (void) fi;

    if(!path) return -ENOSYS;
    
    struct chunked_buffer* r = call_script_stdout("readdir", path);
    if(!r) return -EBADF;
    
    long long int offset_ = 0;

    for(;;) {
        char buf_[EXECFUSE_CHUNKSIZE];
        int ret = chunked_buffer_read(r, buf_, EXECFUSE_CHUNKSIZE-1, offset_);
        buf_[ret]=0;
        
        struct stat st;
        int l = scanstat(buf_, &st);
        if(l==0) break;        
        
        const char* filename = buf_+l;
        
        if (filler(buf, filename, &st, 0)) {
            break;
        }
        
        char* z = strchr(buf_+l, 0);
        if(!z) break;
        offset_ += (z-buf_+1);
    }
    
    return 0;
}

static int execfuse_readlink(const char *path, char *buf, size_t size)
{
    struct chunked_buffer* r = call_script_stdout("readlink", path);
    if(!r) return -EBADF;
    
    int size_to_copy = chunked_buffer_getlen(r);
    if(size_to_copy>size-1) size_to_copy = size-1;
    
    chunked_buffer_read(r, buf, size_to_copy, 0);
    
    buf[size_to_copy]=0;
    
    return 0;
}

struct myinfo {
    sem_t sem;
    unsigned char *tmpbuf;
    long long int offset_for_sript;
    struct chunked_buffer* content;
    int file_was_read;
    int file_was_written;
    int readonly;
    int writeonly;
    int failed;
    int backend_fd;
};

void setenv_mountpoint(int argc, char** argv)
{
    char* mp_buf = malloc(EXECFUSE_MAX_PATHLEN);
    if(mp_buf == NULL) abort();
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if(fuse_parse_cmdline(&args, &mp_buf, NULL, NULL)!=0) abort();
    char* mountpoint;
    if(asprintf(&mountpoint, "%s", mp_buf)==-1) abort();
    free(mp_buf);
    setenv("EXECFUSE_MOUNTPOINT", mountpoint, 1);
    free(mountpoint);
}


static int call_script_ll(const char* script_name, 
                        const char*const* params, 
                        read_t stdin_fn, void* stdin_obj,
                        write_t stdout_fn, void* stdout_obj) {
    return execute_script(
             working_directory
            ,script_name
            ,addargs
            ,params
            ,stdin_fn, stdin_obj
            ,stdout_fn, stdout_obj
        );
}

static int call_script_simple(const char* script_name, const char* param) {
    const char* params[]={param, NULL};
    return call_script_ll(script_name, params, NULL, NULL, NULL, NULL);
}
static int call_script_simple2(const char* script_name, const char* param1, const char* param2) {
    const char* params[]={param1, param2, NULL};
    return call_script_ll(script_name, params, NULL, NULL, NULL, NULL);
}
static int call_script_simple3(const char* script_name, const char* param1, 
                               const char* param2, const char* param3) {
    const char* params[]={param1, param2, param3, NULL};
    return call_script_ll(script_name, params, NULL, NULL, NULL, NULL);
}

struct chuncked_buffer_with_cursor {
    struct chunked_buffer *content;
    long long int offset;
};

static int call_script_stdout_write(void* ii, const char* buf, int len) {
    struct chuncked_buffer_with_cursor* i = (struct chuncked_buffer_with_cursor*)ii;
    chunked_buffer_write(i->content, buf, len, i->offset);
    i->offset += len;
    return len;
}

static struct chunked_buffer* call_script_stdout_ret(const char* script_name, const char* param1, const char* param2, int* call_return_code) {
    const char* params[]={param1, param2, NULL};
    struct chuncked_buffer_with_cursor cbuf;
    cbuf.offset = 0;
    cbuf.content = chunked_buffer_new(EXECFUSE_MAX_PATHLEN);
    int ret = call_script_ll(script_name
            ,params
            ,NULL, NULL
            ,&call_script_stdout_write, (void*)&cbuf
        );
    if(call_return_code != NULL) *call_return_code = ret;
    if(ret>0){
        chunked_buffer_delete(cbuf.content);
        return NULL;
    }
    return cbuf.content;
}

static struct chunked_buffer* call_script_stdout(const char* script_name, const char* param) {
    return call_script_stdout_ret(script_name, param, NULL, NULL);
}

static int read_the_file_write(void* ii, const char* buf, int len) {
    struct myinfo* i = (struct myinfo*)ii;
    chunked_buffer_write(i->content, buf, len, i->offset_for_sript);
    i->offset_for_sript += len;
    return len;
}

static int read_the_file(struct myinfo* i, const char* path) {
    const char* params[]={path, NULL};
    i->offset_for_sript = 0;
    return call_script_ll("read_file"
            ,params
            ,NULL, NULL
            ,&read_the_file_write, (void*)i
        );
}

static int write_the_file_read(void* ii, char* buf, int len) {
    struct myinfo* i = (struct myinfo*)ii;
    int ret = chunked_buffer_read(i->content, buf, len, i->offset_for_sript);
    i->offset_for_sript += ret;
    return ret;
}

static int write_the_file(struct myinfo* i, const char* path) {
    const char* params[]={path, NULL};
    i->offset_for_sript = 0;
    return call_script_ll("write_file"
            ,params
            ,&write_the_file_read, (void*)i
            ,NULL, NULL
        );
    return 0;
}

static int execfuse_open_internal(const char *path, struct fuse_file_info *fi, mode_t mode)
{
    int open_err;
    struct chunked_buffer* backend_file_buf;
    struct myinfo* info;
    char * script_name;
    bool internal;
    int fd;

    STATIC_MODE_STRING(mode, modestr);
    
    if(fi->flags & O_CREAT)
    {
        script_name = "create";
    }
    else
    {
        script_name = "open";
        modestr[0] = 0;
    }

    backend_file_buf = call_script_stdout_ret(script_name, path, modestr, &open_err);

    if(!open_err)
    {
        /* exec script indicated that there is no error opening this file */
        
        /* try to read physical file path */
        char backend_file[EXECFUSE_MAX_PATHLEN];
        int len = chunked_buffer_read(backend_file_buf, backend_file, EXECFUSE_MAX_PATHLEN-1, 0);
        backend_file[len] = 0;
        chunked_buffer_delete(backend_file_buf);
        
        if(len == 0)
        {
            /* exec script did not give any physical file path */
            /* falling back to internal file descriptor management */
            internal = TRUE;
        }
        else
        {
            internal = FALSE;
            
            fd = open(backend_file, fi->flags, mode);
            if(fd == -1)
            {
                return -errno;
            }
        }
    }
    else if(open_err == ENOSYS)
    {
        /* no implemented exec script for open (create) */
        /* faling back to internal file descriptor management */
        internal = TRUE;
    }
    else
    {
        /* exec script indicated error while opening this file */
        /* reflecting the error code to fuse */
        return -open_err;
    }
    
    /* allocate file info object */
    info = (struct myinfo*)malloc(sizeof *info);
    sem_init(&info->sem, 0, 1);
    
    if(internal)
    {
        info->tmpbuf=(unsigned char*)malloc(EXECFUSE_CHUNKSIZE);
        info->content = chunked_buffer_new(EXECFUSE_CHUNKSIZE);
        info->file_was_read = 0;
        info->file_was_written = 0;
        info->readonly = fi->flags & O_RDONLY ;
        info->writeonly = fi->flags & O_WRONLY;
    }
    else
    {
        info->tmpbuf = NULL;
        info->content = NULL;
        info->backend_fd = fd;
    }
    
    info->failed = 0;
    fi->fh = (uintptr_t)  info;

    return 0;
}

static int execfuse_open(const char *path, struct fuse_file_info *fi)
{
    return execfuse_open_internal(path, fi, -1);
}

static int execfuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    fi->flags |= O_CREAT;
    int ret = execfuse_open_internal(path, fi, mode);
    return ret;
}

static int execfuse_fgetattr(const char *path, struct stat *stbuf,
            struct fuse_file_info *fi)
{
    struct myinfo* info = (struct myinfo*)(uintptr_t)  fi->fh;
    
    if(!info || info->content)
    {
        /* Stub for creating files */
        
        (void) path;
        memset(stbuf, 0, sizeof(*stbuf));
        stbuf->st_mode = S_IFREG;
        stbuf->st_blksize = 512;    
    
        return 0;
    }
    else
    {
        return fstat(info->backend_fd, stbuf);
    }
}

static int execfuse_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
    if(!i) return -ENOSYS;
    
    if(i->content)
    {
        if (i->writeonly || i->failed) {
            return -EBADF;
        }
        
        sem_wait(&i->sem);
        
        int ret = 0;
    
        
        if(!i->file_was_read && !i->file_was_written) {
            ret = read_the_file(i, path);
            i->file_was_read = 1;
            if(ret>0) {
                i->failed = 1;
                sem_post(&i->sem);
                return -ret;    
            }
        }
        
        ret = chunked_buffer_read(i->content, buf, size, offset);
        
        sem_post(&i->sem);
        return ret;
    }
    else
    {
        return pread(i->backend_fd, buf, size, offset);
    }
}


static int execfuse_write(const char *path, const char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
    if(!i) return -ENOSYS;
    
    if(i->content)
    {
        if (i->readonly || i->failed) {
            return -EBADF;
        }
        
        sem_wait(&i->sem);
        
        int ret = 0;
        
        i->file_was_written = 1;
    
        ret = chunked_buffer_write(i->content, buf, size, offset);
    
    
        sem_post(&i->sem);
        return ret;
    }
    else
    {
        return pwrite(i->backend_fd, buf, size, offset);
    }
}

static int execfuse_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    
    struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
    if(!i) return -ENOSYS;

    int ret = 0;
    
    if(i->content)
    {
        if(i->file_was_written && !i->failed) {
            ret = write_the_file(i, path);
        }
        
        sem_destroy(&i->sem);
        chunked_buffer_delete(i->content);
        free(i->tmpbuf);
    }
    else
    {
        /* call script when closing file which was opened on the physical fs */
        ret = call_script_simple("close", path);
        if(ret == 0)
        {
            /* ok, the script let us close the file */
            if(close(i->backend_fd) != 0)
            {
                ret = errno;
            }
        }
        /* return success or the error code from the script or from actual close() */
    }
    
    free(i);

    return -ret;
}

static int execfuse_ftruncate(const char *path, off_t size,
             struct fuse_file_info *fi)
{    
    struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
    if(!i) return -ENOSYS;
    
    if(i->content)
    {
        if (i->readonly || i->failed) {
            return -EBADF;
        }
        
        sem_wait(&i->sem);
        
        int ret = 0;
        
        chunked_buffer_truncate(i->content, size);
        
        sem_post(&i->sem);
        return ret;
    }
    else
    {
        if(ftruncate(i->backend_fd, size) != 0)
        {
            return -errno;
        }
        return 0;
    }
}

static int execfuse_truncate(const char *path, off_t size)
{
    char b[256];
    sprintf(b, "%lld", (long long int) size);
    return -call_script_simple2("truncate", path, b);
}


static int execfuse_mknod(const char *path, mode_t mode, dev_t rdev)
{
    if (S_ISFIFO(mode)) {
        return -call_script_simple("mkfifo", path);
    } else {
        char b[256];
        sprintf(b, "0x%016llx", (long long int)rdev);
        return -call_script_simple2("mknod", path, b);
    }
}

static int execfuse_mkdir(const char *path, mode_t mode)
{
    STATIC_MODE_STRING(mode, b);
    return -call_script_simple2("mkdir", path, b);
}

static int execfuse_unlink(const char *path)
{
    return -call_script_simple("unlink", path);
}

static int execfuse_rmdir(const char *path)
{
    return -call_script_simple("rmdir", path);
}

static int execfuse_symlink(const char *from, const char *to)
{
    return -call_script_simple2("symlink", from, to);
}

static int execfuse_rename(const char *from, const char *to)
{
    return -call_script_simple2("rename", from, to);
}

static int execfuse_link(const char *from, const char *to)
{
    return -call_script_simple2("link", from, to);
}

static int execfuse_chmod(const char *path, mode_t mode)
{
    STATIC_MODE_STRING(mode, b);
    return -call_script_simple2("chmod", path, b);
}

static int execfuse_chown(const char *path, uid_t uid, gid_t gid)
{
    char b1[64];
    char b2[64];
    sprintf(b1, "%d", uid);
    sprintf(b2, "%d", gid);
    return -call_script_simple3("chown", path, b1, b2);
}

static int execfuse_utimens(const char *path, const struct timespec ts[2])
{
    double atime;
    double mtime;
    
    atime = ts[0].tv_sec + ts[0].tv_nsec*0.000000001;
    mtime = ts[1].tv_sec + ts[1].tv_nsec*0.000000001;
    
    char b1[64];
    char b2[64];
    sprintf(b1, "%.10lf", atime);
    sprintf(b2, "%.10lf", mtime);
    return -call_script_simple3("utimens", path, b1, b2);
}

void* execfuse_init(struct fuse_conn_info *conn) {
    call_script_simple("init", SCRIPT_API_VERSION);
    return NULL;    
}
void execfuse_destroy (void * arg) {
    call_script_simple("destroy", SCRIPT_API_VERSION);    
}

static struct fuse_operations execfuse_oper = {
    .getattr    = execfuse_getattr,
    .readdir    = execfuse_readdir,
    .readlink   = execfuse_readlink,
    .open        = execfuse_open,
    .create        = execfuse_create,
    .fgetattr    = execfuse_fgetattr,
    .read        = execfuse_read,
    .write        = execfuse_write,
    .ftruncate  = execfuse_ftruncate,
    .release    = execfuse_release,
    
    .truncate  = execfuse_truncate,
    
    
    .mknod        = execfuse_mknod,
    .mkdir        = execfuse_mkdir,
    .symlink    = execfuse_symlink,
    .unlink        = execfuse_unlink,
    .rmdir        = execfuse_rmdir,
    .rename        = execfuse_rename,
    .link        = execfuse_link,
    .chmod        = execfuse_chmod,
    .chown        = execfuse_chown,
    .utimens    = execfuse_utimens,
    
    .init       = execfuse_init,
    .destroy    = execfuse_destroy,
    
    
    .flag_nullpath_ok = 1,
};

int main(int argc, char *argv[])
{
    if(argc<3) {
        fprintf(stderr, "Usage: execfuse scripts_directory mountpoint [FUSE options] [-- additional arguments]\n");
        return 1;
    }
    
    int i;
    for(i=3; i<argc; ++i) {
        if(!strcmp(argv[i], "--")) {
            addargs = (const char**)argv+i+1;
            argv[i]=NULL;
            argc=i;
            break;
        }
    }
    
    int cd = open(".", O_DIRECTORY);
    if (cd == -1) {perror("open"); return 2; }
    if(chdir(argv[1])) {
        perror("chdir");
        return 2;
    }
    getcwd(working_directory, EXECFUSE_MAX_PATHLEN);
    fchdir(cd);
    
    {
        char buf[64];
        sprintf(buf, "%d", getpid());
        setenv("EXECFUSE_PID", buf, 1);
    }
    
    int ret = call_script_simple("check_args", SCRIPT_API_VERSION);
    if(ret && ret!=ENOSYS) return ret;
    
    
    setenv_mountpoint(argc-1, argv+1);
    struct fuse_args args = FUSE_ARGS_INIT(argc-1, argv+1);
    fuse_opt_parse(&args, NULL, NULL, NULL);
    fuse_opt_add_arg(&args, "-odirect_io");
    return fuse_main(args.argc, args.argv, &execfuse_oper, NULL);
}
