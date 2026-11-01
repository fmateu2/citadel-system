#ifndef UTILS2_H
#define UTILS2_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define printF(x) write(STDOUT_FILENO, (x), strlen(x))

int asprintf(char **strp, const char *fmt, ...);
char *readUntil(int fd, char separator);
void str_trim(char *s);
char *str_to_upper(const char *src);

#endif
