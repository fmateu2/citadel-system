#include "net_utils.h"
#include "utils2.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

char *dup_string(const char *src) {
    if (src == NULL) {
        return strdup("");
    }
    return strdup(src);
}

char *dup_n_string(const char *src, size_t n) {
    char *dst;

    dst = malloc(n + 1);
    if (dst == NULL) {
        return NULL;
    }

    memcpy(dst, src, n);
    dst[n] = '\0';
    return dst;
}

char *frame_field_to_string(const char *src, size_t len) {
    size_t actual = len;

    while (actual > 0 && src[actual - 1] == '\0') {
        actual--;
    }

    return dup_n_string(src, actual);
}

char *frame_data_to_string_alloc(const Frame *frame) {
    if (frame == NULL) {
        return NULL;
    }

    return dup_n_string((const char *)frame->data, frame->data_length);
}

char *build_origin_string(const char *ip, int port) {
    char *result = NULL;

    if (ip == NULL) {
        return NULL;
    }

    if (asprintf(&result, "%s:%d", ip, port) < 0) {
        return NULL;
    }

    return result;
}

int fill_fixed_field(char *dst, size_t dst_size, const char *src) {
    size_t len;

    if (dst == NULL || src == NULL) {
        return -1;
    }

    len = strlen(src);
    if (len >= dst_size) {
        return -1;
    }

    memset(dst, 0, dst_size);
    memcpy(dst, src, len);
    return 0;
}

int parse_origin_alloc(const char origin[ORIGIN_SIZE], char **ip, int *port) {
    char *origin_str;
    char *sep;
    char *ip_copy;

    if (ip == NULL || port == NULL) {
        return -1;
    }

    *ip = NULL;
    *port = 0;

    origin_str = frame_field_to_string(origin, ORIGIN_SIZE);
    if (origin_str == NULL) {
        return -1;
    }

    sep = strrchr(origin_str, ':');
    if (sep == NULL) {
        free(origin_str);
        return -1;
    }

    *sep = '\0';
    sep++;

    ip_copy = dup_string(origin_str);
    if (ip_copy != NULL) {
        *ip = ip_copy;
        *port = atoi(sep);
    }

    free(origin_str);
    if (ip_copy != NULL) {
        return 0;
    }
    return -1;
}

char *frame_destination_alloc(const Frame *frame) {
    if (frame == NULL) {
        return NULL;
    }

    return frame_field_to_string(frame->destination, DESTINATION_SIZE);
}

void split_two_fields_alloc(const char *payload, char **left, char **right) {
    const char *sep;

    if (left != NULL) {
        *left = NULL;
    }
    if (right != NULL) {
        *right = NULL;
    }

    if (payload == NULL) {
        return;
    }

    sep = strchr(payload, '&');
    if (sep == NULL) {
        if (left != NULL) {
            *left = dup_string(payload);
        }
        if (right != NULL) {
            *right = dup_string("");
        }
        return;
    }

    if (left != NULL) {
        *left = dup_n_string(payload, (size_t)(sep - payload));
    }
    if (right != NULL) {
        *right = dup_string(sep + 1);
    }
}

char **split_fields_alloc(const char *payload, char sep_char, int *count) {
    int n = 1;
    int i;
    const char *p;
    char **fields;
    const char *start;

    if (count != NULL) {
        *count = 0;
    }

    if (payload == NULL) {
        return NULL;
    }

    for (p = payload; *p != '\0'; p++) {
        if (*p == sep_char) {
            n++;
        }
    }

    fields = calloc((size_t)n, sizeof(char *));
    if (fields == NULL) {
        return NULL;
    }

    start = payload;
    i = 0;

    for (p = payload;; p++) {
        if (*p == sep_char || *p == '\0') {
            fields[i] = dup_n_string(start, (size_t)(p - start));
            if (fields[i] == NULL) {
                int j;
                for (j = 0; j < i; j++) {
                    free(fields[j]);
                }
                free(fields);
                return NULL;
            }
            i++;
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }

    if (count != NULL) {
        *count = n;
    }

    return fields;
}

void free_split_fields(char **fields, int count) {
    int i;

    if (fields == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        free(fields[i]);
    }
    free(fields);
}

/* Computes MD5 via md5sum piped through stdin. Returns 33-byte heap string or NULL. */
char *md5_bytes_alloc(const unsigned char *data, size_t len) {
    int inpipe[2], outpipe[2];
    pid_t pid;
    char hash[33] = {0};
    size_t written = 0;
    ssize_t nread;

    if (pipe(inpipe) < 0) {
        return NULL;
    }
    if (pipe(outpipe) < 0) {
        close(inpipe[0]);
        close(inpipe[1]);
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(inpipe[0]);
        close(inpipe[1]);
        close(outpipe[0]);
        close(outpipe[1]);
        return NULL;
    }

    if (pid == 0) {
        close(inpipe[1]);
        close(outpipe[0]);
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]);
        close(outpipe[1]);
        execlp("md5sum", "md5sum", "-", (char *)NULL);
        _exit(1);
    }

    close(inpipe[0]);
    close(outpipe[1]);

    while (written < len) {
        ssize_t n = write(inpipe[1], data + written, len - written);
        if (n <= 0) {
            break;
        }
        written += (size_t)n;
    }
    close(inpipe[1]);

    nread = read(outpipe[0], hash, 32);
    close(outpipe[0]);
    waitpid(pid, NULL, 0);

    if (nread != 32) {
        return NULL;
    }
    hash[32] = '\0';
    return strdup(hash);
}

void print_dynamic_line(const char *fmt, const char *arg) {
    char *line = NULL;

    if (asprintf(&line, fmt, arg) >= 0) {
        printF("\n");
        printF(line);
    }

    free(line);
}
