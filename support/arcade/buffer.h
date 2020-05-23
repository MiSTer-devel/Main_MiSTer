#ifndef BUFFER_H_
#define BUFFER_H_

typedef struct {
  unsigned int length;
  unsigned int capacity;
  unsigned int expand_length;
  char * content;
} buffer_data;

buffer_data * buffer_init(const unsigned int initial_size);
int buffer_append(buffer_data *buffer, const char *append_data);
void buffer_destroy(buffer_data * buffer);

#endif
