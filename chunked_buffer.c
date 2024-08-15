#include <assert.h>
#include <string.h>
//#include <malloc.h>
#include <stdlib.h>

#include "common.h"
#include "chunked_buffer.h"

struct chunked_buffer {
	unsigned long long	total_len;
	char**	buffers;
	int	buffers_capacity;
	int	chunk_size;
};

struct chunked_buffer* chunked_buffer_new(int chunk_size) {
	struct chunked_buffer	*c = (struct chunked_buffer *)malloc(sizeof *c);

	assert(c != NULL);
	assert(chunk_size > 0 && chunk_size < 0x10000000);

	c->buffers = NULL;
	c->total_len = 0;
	c->chunk_size = chunk_size;
	c->buffers_capacity = 16;

	return c;
}

void chunked_buffer_delete(struct chunked_buffer* c) {
	int	i;

	if(c->buffers) {
		for(i = 0; i < c->buffers_capacity; ++i) {
			free(c->buffers[i]);
		}
		free(c->buffers);
	}

	free(c);
}

long long int chunked_buffer_getlen(struct chunked_buffer* c) {
	assert(c != NULL);
	return c->total_len;
}

static int chunked_buffer_write1(struct chunked_buffer* c, const char* buf, int len, long long int offset) {
	int	chunk_number = offset / c->chunk_size;
	int	offset_inside_buffer = offset % c->chunk_size;
	int	size_to_be_written = c->chunk_size - offset_inside_buffer;
	int	i;

	if (size_to_be_written > len)
		size_to_be_written = len;

	if (chunk_number >= c->buffers_capacity) {
		int	saved_capacity = c->buffers_capacity;

		while(chunk_number >= c->buffers_capacity) {
			c->buffers_capacity *= 2;
		}
		if (c->buffers) {
			c->buffers = (char**) realloc(c->buffers, sizeof(char*) * c->buffers_capacity);
			assert(c->buffers != NULL);
			for(i = saved_capacity; i < c->buffers_capacity; ++i) {
				c->buffers[i] = NULL;
			}
		}
	}

	if(!c->buffers) {
		c->buffers = (char**) realloc(c->buffers, sizeof(char*) * c->buffers_capacity);
		assert(c->buffers != NULL);
		for(i = 0; i < c->buffers_capacity; ++i) {
			c->buffers[i] = NULL;
		}
	}

	if (!c->buffers[chunk_number]) {
		c->buffers[chunk_number] = (char*)malloc(c->chunk_size);
		assert(c->buffers[chunk_number] != NULL);
		memset(c->buffers[chunk_number], 0, c->chunk_size);
	}

	memmove(c->buffers[chunk_number] + offset_inside_buffer, buf, size_to_be_written);

	if(c->total_len < offset + size_to_be_written)
		c->total_len = offset + size_to_be_written;

	return size_to_be_written;
}

int chunked_buffer_write(struct chunked_buffer* c, const char* buf, int len, long long int offset) {
	int	accumul_len = 0;
	int	ret;

	assert(offset >= 0 && c != NULL && buf != NULL && len >= 0);

	while(len > 0) {
		ret = chunked_buffer_write1(c, buf, len, offset);
		if(ret < 1)
			break;

		accumul_len += ret;
		len -= ret;
		offset += ret;
		buf += ret;
	}

	return accumul_len;
}


static int chunked_buffer_read1(struct chunked_buffer* c, char* buf, int len, long long int offset) {
	int	chunk_number = offset / c->chunk_size;
	int	offset_inside_buffer = offset % c->chunk_size;
	int	size_to_be_read = c->chunk_size - offset_inside_buffer;
	int	return_zero = 0;

	if (size_to_be_read>len)
		size_to_be_read = len;

	if (chunk_number >= c->buffers_capacity) {
		return_zero = 1;
	}

	if(!c->buffers) {
		return_zero = 1;
	} else
	if (!c->buffers[chunk_number]) {
		return_zero = 1;
	}

	if(return_zero) {
		memset(buf, 0, size_to_be_read);
		return size_to_be_read;
	}

	memmove(buf, c->buffers[chunk_number] + offset_inside_buffer, size_to_be_read);

	return size_to_be_read;
}

int chunked_buffer_read(struct chunked_buffer* c, char* buf, int len, long long int offset) {
	int accumul_len=0;
	int ret;

	assert(offset >= 0 && c != NULL && buf != NULL && len >= 0);

	if(c->total_len <= offset)
		return 0;
	if(len > c->total_len - offset)
		len = c->total_len - offset;

	while(len > 0) {
		ret = chunked_buffer_read1(c, buf, len, offset);
		if(ret < 1)
			break;

		accumul_len += ret;
		len -= ret;
		offset += ret;
		buf+=ret;
	}

	return accumul_len;
}


void chunked_buffer_truncate(struct chunked_buffer* c, long long int len) {
	c->total_len = len;
}
