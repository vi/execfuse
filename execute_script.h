#pragma once

typedef int (*read_t )(void* obj,       char *buf, int count);
typedef int (*write_t)(void* obj, const char *buf, int count);

/**
 * Execute directory/script_name with argv [script_name]+params+appended_params.
 * return exit code of the script
 * if stdin_fn not NULL, use this function to provide script's stdin
 *     (the function will be called immediately if it fais with EINTR/EAGAIN)
 * if stdout_fn is not NULL, it will be called for script's stdout
 *     (the fuction should not fail, otherwise script output will be trimmed)
 * stdin_obj and stdout_obj are passed to respective functions
 *
 * appended_params and/or params can be NULL
 */
int execute_script(
			const char* directory,
			const char* script_name,
			const char*const* appended_params,
			const char*const* params,
			read_t stdin_fn, void* stdin_obj,
			write_t stdout_fn, void* stdout_obj
			);
