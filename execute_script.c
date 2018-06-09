#include <malloc.h>
#include <assert.h>

#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>


#include "common.h"
#include "execute_script.h"

int execute_script(
                        const char* directory,
						const char* script_name, 
						const char*const* appended_params,
						const char*const* params, 
						read_t stdin_fn, void* stdin_obj,
                        write_t stdout_fn, void* stdout_obj
						) {
fprintf(stderr, "execute_script(%s, %s, %s", directory, script_name, params[0]);
int debug_n;
for(debug_n=1; params[debug_n]; debug_n++) fprintf(stderr, ", %s", params[debug_n]);
fprintf(stderr, ")\n");
    
	int ppcount=0;
	int i;
	int pcount=0;
	if(appended_params) {
		for(i=0; appended_params[i]; ++i) ++ppcount;	
	}
	if(params) {
		for(i=0; params[i]; ++i) ++pcount;	
	}
	const char** argv = (const char**)malloc((ppcount+pcount+2)*sizeof(char*));
	assert(argv!=NULL);
	
	char script_path[EXECFUSE_MAX_PATHLEN];
	
	sprintf(script_path, "%s/%s", directory, script_name);

	argv[0]=script_name;
	for(i=0; i<pcount; ++i) argv[i+1] = params[i];
	for(i=0; i<ppcount; ++i) argv[i+1+pcount] = appended_params[i];
	argv[1+ppcount+pcount]=NULL;
	
	int child_stdout = -1;
	int child_stdin = -1;
	
	int to_be_written = -1;
	int to_be_read = -1;
	
	if(stdin_fn)
	   {int p[2]; int ret = pipe(p); assert(ret==0); to_be_written=p[1]; child_stdin =p[0];}
	if(stdout_fn)
	   {int p[2]; int ret = pipe(p); assert(ret==0); to_be_read   =p[0]; child_stdout=p[1];}
	
	
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    sigprocmask(SIG_BLOCK, &mask, NULL);
        
	int childpid = fork();
	if(!childpid) {
	   if(child_stdout!=-1) {
	       close(to_be_read);
	       dup2(child_stdout, 1);
	       close(child_stdout);
	   }
	   if(child_stdin!=-1) {
	       close(to_be_written);
	       dup2(child_stdin, 0);
	       close(child_stdin);
	   }
	   execv(script_path, (char**)argv);
	   _exit(ENOSYS);
	}
	
    if(child_stdout!=-1) {
        close(child_stdout);
    }
    if(child_stdin!=-1) {
        close(child_stdin);
    }
	
    /* Event loop for feeding the script with input data, reading output and reading exit code */
    
    int maxfd = -1;
    if(to_be_written!=-1 && maxfd<to_be_written) maxfd = to_be_written;
    if(to_be_read!=-1    && maxfd<to_be_read   ) maxfd = to_be_read   ;
    ++maxfd;
    
    char buf[EXECFUSE_MAX_FILESIZE];
    fd_set rfds;
    fd_set wfds;
    
    for(;;) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        if(to_be_read!=-1) FD_SET(to_be_read, &rfds);
        if(to_be_written!=-1) FD_SET(to_be_written, &wfds);
        
        if(to_be_read == -1 && to_be_written == -1) break;
        
        int ret = select(maxfd+1, &rfds, &wfds, NULL, NULL);
        
        if (ret == -1) {
            if (errno==EINTR || errno==EAGAIN) continue;
            break;
        }
        
        if (to_be_read!=-1 && FD_ISSET(to_be_read, &rfds)) {
            ret = read(to_be_read, buf, sizeof buf);
            if (ret==-1) {
                if (errno == EINTR || errno==EAGAIN) continue;
            }
            if (ret==0 || ret == -1) {
                close(to_be_read);
                to_be_read = -1;
            } else {
                int ret2 = (*stdout_fn)(stdout_obj, buf, ret);
                if (ret2<ret) {
                    close(to_be_read);
                    to_be_read = -1;
                }
            }
        }
        
        if (to_be_written!=-1 && FD_ISSET(to_be_written, &wfds)) {
            ret = (*stdin_fn)(stdin_obj, buf, sizeof buf);
            if (ret==-1) {
                if (errno == EINTR || errno==EAGAIN) continue;
            }
            if(!ret || ret == -1) {
                close(to_be_written);
                to_be_written=-1;
            } else {
                ret = write(to_be_written, buf, ret);
                if (ret==-1) {
                    if (errno == EINTR || errno==EAGAIN) continue;
                }
                if(ret==0 || ret == -1) {
                    close(to_be_written);
                    to_be_read = -1;
                }
            }
        }
    }
	
    if(to_be_read!=-1) close(to_be_read);
    if(to_be_written!=-1) close(to_be_read);
    
    int status;
    waitpid(childpid, &status, 0);
    int exit_code = WEXITSTATUS(status);
    
	free(argv);
	return exit_code;
}
