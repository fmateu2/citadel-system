#ifndef NET_UTILS_H
#define NET_UTILS_H

#include "protocol2.h"
#include <stddef.h>

char *dup_string(const char *src);
char *dup_n_string(const char *src, size_t n);
char *frame_field_to_string(const char *src, size_t len);
char *frame_data_to_string_alloc(const Frame *frame);
char *build_origin_string(const char *ip, int port);
int fill_fixed_field(char *dst, size_t dst_size, const char *src);
int parse_origin_alloc(const char origin[ORIGIN_SIZE], char **ip, int *port);
char *frame_destination_alloc(const Frame *frame);
void split_two_fields_alloc(const char *payload, char **left, char **right);
char **split_fields_alloc(const char *payload, char sep_char, int *count);
void free_split_fields(char **fields, int count);
char *md5_bytes_alloc(const unsigned char *data, size_t len);
void print_dynamic_line(const char *fmt, const char *arg);

#endif
