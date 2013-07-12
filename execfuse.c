
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



static int execfuse_getattr(const char *path, struct stat *stbuf)
{
	if(!path) return -ENOSYS;
	
	
	
    if(!strcmp(path,"/")) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 1;
        return 0;   
    }
    
    stbuf->st_size = 44444;
    stbuf->st_blocks = stbuf->st_size / 512;
    stbuf->st_mode = S_IFREG | 0666;
	return 0;
}


static int execfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi) {
    
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	dp = opendir(working_directory);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		
		size_t l = strlen(de->d_name);
		if(!strncasecmp(de->d_name+l-4, ".dsc", 4)) {
		    de->d_name[l-4]=0; // trim .dsc
    		if (filler(buf, de->d_name, &st, 0))
    			break;
		}
	}

	closedir(dp);
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


int read_the_file_write(void* ii, const char* buf, int len) {
	struct myinfo* i = (struct myinfo*)ii;
	chunked_buffer_write(i->content, buf, len, i->offset_for_sript);
	i->offset_for_sript += len;
	return len;
}
static int read_the_file(struct myinfo* i, const char* path) {
	const char* params[]={path, NULL};
	i->offset_for_sript = 0;
	return execute_script(
			 working_directory
			,"read_file"
			,NULL
			,params
			,NULL, NULL
			,&read_the_file_write, (void*)i
		);
}

static int write_the_file(struct myinfo* i, const char* path) {
	return 0;
}

static int execfuse_open(const char *path, struct fuse_file_info *fi)
{
    struct stat stbuf;
    int ret = execfuse_getattr(path, &stbuf);
    if(ret) return ret;
    
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
	return execfuse_open(path, fi);
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


static int execfuse_truncate(const char *path, off_t size)
{
	return 0;
}

static struct fuse_operations execfuse_oper = {
	.getattr	= execfuse_getattr,
	.readdir	= execfuse_readdir,
	.open		= execfuse_open,
	.create		= execfuse_create,
	.read		= execfuse_read,
	.write		= execfuse_write,
	.release	= execfuse_release,
	
	.truncate  = execfuse_truncate,
	
	
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
