
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

#include "chunked_buffer.h"
#include "execute_script.h"

char working_directory[4096];


static int call_script_simple(const char* script_name, const char* param);
static int call_script_simple2(const char* script_name, const char* param1, const char* param2);
static struct chunked_buffer* call_script_stdout(const char* script_name, const char* param);



// "ino=2 mode=drwxr-xr-x nlink=35 uid=0 gid=0 rdev=0 size=1224 blksize=512 blocks=2 atime=1365035428.0000000000 mtime=1368450727.0000000000 ctime=1368450727.0000000000 filename\0"
// find / -maxdepth 0 -printf 'ino=%i mode=%M nlink=%n uid=%U gid=%G rdev=0 size=%s blksize=512 blocks=%b atime=%A@ mtime=%T@ ctime=%C@ %f\0'
	
static int scanstat(const char* str, struct stat *stbuf) {
	char mode_ox, mode_ow, mode_or, mode_gx, mode_gw, mode_gr, mode_ux, mode_uw, mode_ur, mode;
	double atime, mtime, ctime;
	int l;
	
	int ret;
	ret = sscanf(str, "ino=%lli mode=%c%c%c%c%c%c%c%c%c%c nlink=%i uid=%i gid=%i "
		"rdev=%lli size=%lli blksize=%li blocks=%lli atime=%lf mtime=%lf ctime=%lf %n"
	     ,&stbuf->st_ino
	     ,&mode, &mode_ur, &mode_uw, &mode_ux, &mode_gr, &mode_gw, &mode_gx, &mode_or, &mode_ow, &mode_ox
	     ,&stbuf->st_nlink
	     ,&stbuf->st_uid
	     ,&stbuf->st_gid
	     ,&stbuf->st_rdev
	     ,&stbuf->st_size
	     ,&stbuf->st_blksize
	     ,&stbuf->st_blocks
	     ,&atime, &mtime, &ctime
	     ,&l
	 	);
	
	if(ret!= 21) {
		return 0;
	}
	
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
	
	char buf[65536];
	int ret = chunked_buffer_read(r, buf, 65535, 0);
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
		char buf_[65536];
		int ret = chunked_buffer_read(r, buf_, 65535, offset_);
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
};


static int call_script_ll(const char* script_name, 
						const char*const* params, 
						read_t stdin_fn, void* stdin_obj,
                        write_t stdout_fn, void* stdout_obj) {
	return execute_script(
			 working_directory
			,script_name
			,NULL
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

static struct chunked_buffer* call_script_stdout(const char* script_name, const char* param) {
	const char* params[]={param, NULL};
	struct chuncked_buffer_with_cursor i;
	i.offset = 0;
	i.content = chunked_buffer_new(4096);
	int ret = call_script_ll(script_name
			,params
			,NULL, NULL
			,&call_script_stdout_write, (void*)&i
		);
	if(ret>0){
		chunked_buffer_delete(i.content);
		return NULL;
	}
	return i.content;
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

static int execfuse_open(const char *path, struct fuse_file_info *fi)
{
    struct myinfo* i = (struct myinfo*)malloc(sizeof *i);
   
    sem_init(&i->sem, 0, 1);
	
	i->tmpbuf=(unsigned char*)malloc(65536);
	i->content = chunked_buffer_new(65536);
	i->file_was_read = 0;
	i->file_was_written = 0;
	i->readonly = fi->flags & O_RDONLY ;
	i->writeonly = fi->flags & O_WRONLY;
	i->failed = 0;
    
    fi->fh = (uintptr_t)  i;
    
    return 0;
}

static int execfuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int ret = execfuse_open(path, fi);
	return ret;
}

static int execfuse_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	/* Stub for creating files */
	
	(void) path;
	memset(stbuf, 0, sizeof(*stbuf));
	stbuf->st_mode = S_IFREG;
	stbuf->st_blksize = 512;	

	return 0;
}

static int execfuse_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
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


static int execfuse_write(const char *path, const char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
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

static int execfuse_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
	
	int ret = 0;
	
	if(i->file_was_written && !i->failed) {
		ret = write_the_file(i, path);
	}
	
	sem_destroy(&i->sem);
	chunked_buffer_delete(i->content);
	free(i->tmpbuf);
	free(i);

	return -ret;
}

static int execfuse_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{	
	struct myinfo* i = (struct myinfo*)(uintptr_t)  fi->fh;
	if(!i) return -ENOSYS;
	
	if (i->readonly || i->failed) {
		return -EBADF;
	}
	
    sem_wait(&i->sem);
	
	int ret = 0;
	
	chunked_buffer_truncate(i->content, size);
	
    sem_post(&i->sem);
	return ret;
}

static int execfuse_truncate(const char *path, off_t size)
{
	char b[256];
	sprintf(b, "%lld", size);
	return -call_script_simple2("truncate", path, b);
}


static int execfuse_mknod(const char *path, mode_t mode, dev_t rdev)
{
	if (S_ISFIFO(mode)) {
		return -call_script_simple("mkfifo", path);
	} else {
		char b[256];
		sprintf(b, "0x%016llx", rdev);
		return -call_script_simple2("mknod", path, b);
	}
}

static int execfuse_mkdir(const char *path, mode_t mode)
{
	return -call_script_simple("mkdir", path);
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


static struct fuse_operations execfuse_oper = {
	.getattr	= execfuse_getattr,
	.readdir	= execfuse_readdir,
	.readlink   = execfuse_readlink,
	.open		= execfuse_open,
	.create		= execfuse_create,
	.fgetattr	= execfuse_fgetattr,
	.read		= execfuse_read,
	.write		= execfuse_write,
	.ftruncate  = execfuse_ftruncate,
	.release	= execfuse_release,
	
	.truncate  = execfuse_truncate,
	
	
	.mknod		= execfuse_mknod,
	.mkdir		= execfuse_mkdir,
	.symlink	= execfuse_symlink,
	.unlink		= execfuse_unlink,
	.rmdir		= execfuse_rmdir,
	.rename		= execfuse_rename,
	.link		= execfuse_link,
	
	
	.flag_nullpath_ok = 1,
};

int main(int argc, char *argv[])
{
    if(argc<3) {
        fprintf(stderr, "Usage: execfuse directory mountpoint [FUSE options]\n");
        return 1;
    }
    
    int cd = open(".", O_DIRECTORY);
    if(chdir(argv[1])) {
        perror("chdir");
        return 2;
    }
    getcwd(working_directory, 4096);
    fchdir(cd);
    
	return fuse_main(argc-1, argv+1, &execfuse_oper, NULL);
}
