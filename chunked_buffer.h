#pragma once


struct chunked_buffer;

struct chunked_buffer* chunked_buffer_new(int chunk_size);
void chunked_buffer_delete(struct chunked_buffer* c);

long long int chunked_buffer_getlen(struct chunked_buffer* c);

int chunked_buffer_write(struct chunked_buffer* c, const char* buf, int len, long long int offset);
int chunked_buffer_read(struct chunked_buffer* c, char* buf, int len, long long int offset);

void chunked_buffer_truncate(struct chunked_buffer* c, long long int len);
