#pragma once

typedef int (*read_t )(void* obj,       char *buf, int count);
typedef int (*write_t)(void* obj, const char *buf, int count);

int execute_script(
                        const char* directory,
						const char* script_name, 
						const char*const* prepended_params,
						const char* param, 
						read_t stdin_fn, void* stdin_obj,
                        write_t stdout_fn, void* stdout_obj
						);
