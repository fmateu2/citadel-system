/* utils2.c — General-purpose utilities: buffered line reader, string trimming, uppercasing. */
#include "utils2.h"

/* readUntil — Reads from fd one byte at a time until separator, returns heap-allocated string. */
char *readUntil(int fd, char separator) {
    char ch;
    int size = 16;
    int len = 0;
    char *buffer = malloc(size);
    char *tmp;

    if (buffer == NULL) {
        return NULL;
    }

    while (read(fd, &ch, 1) == 1) {
        if (ch == separator) {
            buffer[len] = '\0';
            return buffer;
        }

        if (len >= size - 1) {
            size *= 2;
            tmp = realloc(buffer, size);
            if (tmp == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = tmp;
        }

        buffer[len++] = ch;
    }

    if (len == 0) {
        free(buffer);
        return NULL;
    }

    buffer[len] = '\0';
    return buffer;
}

/* str_trim — Removes leading and trailing whitespace from a string in-place. */
void str_trim(char *s) {
    int start = 0;
    int end;

    if (s == NULL) {
        return;
    }

    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }

    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }

    end = (int)strlen(s) - 1;
    while (end >= 0 && isspace((unsigned char)s[end])) {
        s[end] = '\0';
        end--;
    }
}

/* str_to_upper — Returns a heap-allocated uppercased copy of the string. */
char *str_to_upper(const char *src) {
    char *copy;
    int i;

    if (src == NULL) {
        return NULL;
    }

    copy = strdup(src);
    if (copy == NULL) {
        return NULL;
    }

    for (i = 0; copy[i] != '\0'; i++) {
        copy[i] = (char)toupper((unsigned char)copy[i]);
    }

    return copy;
}
