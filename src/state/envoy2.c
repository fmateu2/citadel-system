/* envoy2.c — Envoy lifecycle: persistent worker processes communicate with Maester via pipes. */
#include "envoy2.h"
#include "net_transfer.h"
#include "protocol2.h"
#include "net_frame.h"
#include "utils2.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* read_line_from_fd — Reads one '\n'-terminated line into buf (max maxlen-1 chars + NUL). */
static ssize_t read_line_from_fd(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    ssize_t n;
    char c;

    while (i < maxlen - 1) {
        n = read(fd, &c, 1);
        if (n <= 0) {
            buf[i] = '\0';
            return n;
        }
        buf[i++] = c;
        if (c == '\n') {
            break;
        }
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

/* read_file_alloc_local — Reads a file into a heap buffer. Caller frees. */
static unsigned char *read_file_alloc_local(const char *path, size_t *len) {
    int fd;
    unsigned char *buf = NULL;
    unsigned char *tmp;
    size_t total = 0;
    ssize_t n;
    size_t capacity = 4096;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    buf = malloc(capacity);
    if (buf == NULL) {
        close(fd);
        return NULL;
    }

    while ((n = read(fd, buf + total, capacity - total)) > 0) {
        total += (size_t)n;
        if (total == capacity) {
            capacity *= 2;
            tmp = realloc(buf, capacity);
            if (tmp == NULL) {
                free(buf);
                close(fd);
                return NULL;
            }
            buf = tmp;
        }
    }

    close(fd);

    if (n < 0) {
        free(buf);
        return NULL;
    }

    *len = total;
    return buf;
}

/* envoy_worker_loop — Runs in the forked child. Reads tasks from read_fd, executes sends,
   writes "OK\n" or "FAIL\n" to write_fd. Exits when read_fd reaches EOF. */
static void envoy_worker_loop(int read_fd, int write_fd) {
    char line[1024];
    ssize_t n;

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    /* Ensure these fds are not inherited by any grandchildren (e.g. md5sum) */
    fcntl(read_fd, F_SETFD, FD_CLOEXEC);
    fcntl(write_fd, F_SETFD, FD_CLOEXEC);

    while ((n = read_line_from_fd(read_fd, line, sizeof(line))) > 0) {
        char *p = line;
        char *tok;
        char *target_ip = NULL;
        int target_port = 0;
        char *dest_realm = NULL;
        char *sender_realm = NULL;
        char *sender_ip = NULL;
        int sender_port = 0;
        int ok = 0;

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        tok = strtok_r(p, " ", &p);
        if (tok == NULL) {
            continue;
        }

        if (strncmp(tok, "PLEDGE", 6) == 0) {
            /* Format: PLEDGE <target_ip> <target_port> <dest_realm> <sender_realm> <sender_ip>
             * <sender_port> <sigil_path> */
            char *target_port_str = NULL;
            char *sender_port_str = NULL;
            char *sigil_path = NULL;
            unsigned char *sigil_buf = NULL;
            size_t sigil_len = 0;

            target_ip = strtok_r(NULL, " ", &p);
            target_port_str = strtok_r(NULL, " ", &p);
            dest_realm = strtok_r(NULL, " ", &p);
            sender_realm = strtok_r(NULL, " ", &p);
            sender_ip = strtok_r(NULL, " ", &p);
            sender_port_str = strtok_r(NULL, " ", &p);
            sigil_path = strtok_r(NULL, " ", &p);

            if (target_ip == NULL || target_port_str == NULL || dest_realm == NULL ||
                sender_realm == NULL || sender_ip == NULL || sender_port_str == NULL ||
                sigil_path == NULL) {
                write(write_fd, "FAIL\n", 5);
                continue;
            }

            target_port = atoi(target_port_str);
            sender_port = atoi(sender_port_str);

            sigil_buf = read_file_alloc_local(sigil_path, &sigil_len);
            if (sigil_buf == NULL) {
                write(write_fd, "FAIL\n", 5);
                continue;
            }

            if (send_chunked_transfer_direct(target_ip, target_port, sender_ip, sender_port,
                                             sender_realm, dest_realm, TYPE_ALLIANCE_REQUEST,
                                             TYPE_SIGIL_DATA, sigil_path, sigil_buf,
                                             sigil_len) == 0) {
                ok = 1;
            }

            free(sigil_buf);

        } else if (strncmp(tok, "LIST", 4) == 0) {
            /* Format: LIST <target_ip> <target_port> <dest_realm> <sender_realm> <sender_ip>
             * <sender_port> */
            char *target_port_str = NULL;
            char *sender_port_str = NULL;
            Frame request;

            target_ip = strtok_r(NULL, " ", &p);
            target_port_str = strtok_r(NULL, " ", &p);
            dest_realm = strtok_r(NULL, " ", &p);
            sender_realm = strtok_r(NULL, " ", &p);
            sender_ip = strtok_r(NULL, " ", &p);
            sender_port_str = strtok_r(NULL, " ", &p);

            if (target_ip == NULL || target_port_str == NULL || dest_realm == NULL ||
                sender_realm == NULL || sender_ip == NULL || sender_port_str == NULL) {
                write(write_fd, "FAIL\n", 5);
                continue;
            }

            target_port = atoi(target_port_str);
            sender_port = atoi(sender_port_str);

            if (prepare_frame(&request, TYPE_PRODUCT_LIST_REQUEST, sender_ip, sender_port,
                              dest_realm, sender_realm, strlen(sender_realm)) == 0 &&
                send_one_way_to_ip_port(target_ip, target_port, &request) == 0) {
                ok = 1;
            }

        } else if (strncmp(tok, "TRADE", 5) == 0) {
            /* Format: TRADE <target_ip> <target_port> <dest_realm> <sender_realm> <sender_ip>
               <sender_port> <payload_len> Followed by payload_len raw bytes on read_fd */
            char *target_port_str = NULL;
            char *sender_port_str = NULL;
            char *payload_len_str = NULL;
            size_t payload_len = 0;
            unsigned char *payload = NULL;
            size_t received = 0;
            ssize_t nr;

            target_ip = strtok_r(NULL, " ", &p);
            target_port_str = strtok_r(NULL, " ", &p);
            dest_realm = strtok_r(NULL, " ", &p);
            sender_realm = strtok_r(NULL, " ", &p);
            sender_ip = strtok_r(NULL, " ", &p);
            sender_port_str = strtok_r(NULL, " ", &p);
            payload_len_str = strtok_r(NULL, " ", &p);

            if (target_ip == NULL || target_port_str == NULL || dest_realm == NULL ||
                sender_realm == NULL || sender_ip == NULL || sender_port_str == NULL ||
                payload_len_str == NULL) {
                write(write_fd, "FAIL\n", 5);
                continue;
            }

            target_port = atoi(target_port_str);
            sender_port = atoi(sender_port_str);
            payload_len = (size_t)atol(payload_len_str);

            if (payload_len == 0) {
                write(write_fd, "FAIL\n", 5);
                continue;
            }

            payload = malloc(payload_len + 1);
            if (payload == NULL) {
                write(write_fd, "FAIL\n", 5);
                continue;
            }

            while (received < payload_len) {
                nr = read(read_fd, payload + received, payload_len - received);
                if (nr <= 0) {
                    break;
                }
                received += (size_t)nr;
            }
            payload[payload_len] = '\0';

            if (received == payload_len) {
                if (send_chunked_transfer_direct(
                        target_ip, target_port, sender_ip, sender_port, sender_realm, dest_realm,
                        TYPE_ORDER_REQUEST_HEADER, TYPE_ORDER_TRANSFER_DATA, "order.txt", payload,
                        payload_len) == 0) {
                    ok = 1;
                }
            }

            free(payload);
        }

        if (ok) {
            write(write_fd, "OK\n", 3);
        } else {
            write(write_fd, "FAIL\n", 5);
        }
    }
}

/* load_envoys — Forks N worker processes, one per envoy; each runs envoy_worker_loop(). */
Envoy *load_envoys(Config cfg) {
    Envoy *envoys;
    int i;
    int j;

    if (cfg.num_envoys <= 0) {
        return NULL;
    }

    envoys = malloc(sizeof(Envoy) * cfg.num_envoys);
    if (envoys == NULL) {
        return NULL;
    }

    for (i = 0; i < cfg.num_envoys; i++) {
        int to_envoy[2];
        int from_envoy[2];
        pid_t pid;

        envoys[i].id = i + 1;
        envoys[i].status = FREE;
        envoys[i].mission_realm = NULL;
        envoys[i].mission_type = 0;
        envoys[i].pid = -1;
        envoys[i].fd_write = -1;
        envoys[i].fd_read = -1;

        if (pipe(to_envoy) < 0 || pipe(from_envoy) < 0) {
            free(envoys);
            return NULL;
        }

        pid = fork();
        if (pid < 0) {
            close(to_envoy[0]);
            close(to_envoy[1]);
            close(from_envoy[0]);
            close(from_envoy[1]);
            free(envoys);
            return NULL;
        }

        if (pid == 0) {
            /* Child: close parent-side ends of this envoy's pipes */
            close(to_envoy[1]);
            close(from_envoy[0]);

            /* Close parent-side ends of all previously created envoys */
            for (j = 0; j < i; j++) {
                close(envoys[j].fd_write);
                close(envoys[j].fd_read);
            }

            envoy_worker_loop(to_envoy[0], from_envoy[1]);
            close(to_envoy[0]);
            close(from_envoy[1]);
            _exit(0);
        }

        /* Parent: close child-side pipe ends */
        close(to_envoy[0]);
        close(from_envoy[1]);

        /* Set close-on-exec so fds are not inherited by grandchildren (e.g. md5sum) */
        fcntl(to_envoy[1], F_SETFD, FD_CLOEXEC);
        fcntl(from_envoy[0], F_SETFD, FD_CLOEXEC);

        envoys[i].pid = pid;
        envoys[i].fd_write = to_envoy[1];
        envoys[i].fd_read = from_envoy[0];
    }

    return envoys;
}

/* free_envoys — Closes write ends (signals EOF to workers), waits for all to exit, frees memory. */
void free_envoys(Envoy *envoys, int count) {
    int i;

    if (envoys == NULL) {
        return;
    }

    /* Signal all envoys to exit */
    for (i = 0; i < count; i++) {
        if (envoys[i].pid > 0) {
            kill(envoys[i].pid, SIGTERM);
        }
        if (envoys[i].fd_write >= 0) {
            close(envoys[i].fd_write);
            envoys[i].fd_write = -1;
        }
    }

    /* Reap all envoy processes */
    for (i = 0; i < count; i++) {
        if (envoys[i].pid > 0) {
            waitpid(envoys[i].pid, NULL, 0);
        }
        if (envoys[i].fd_read >= 0) {
            close(envoys[i].fd_read);
        }
        free(envoys[i].mission_realm);
    }

    free(envoys);
}

/* acquire_free_envoy — Marks the first free envoy as ON_MISSION and returns its id. */
int acquire_free_envoy(Envoy *envoys, int count, const char *realm) {
    int i;

    if (envoys == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (envoys[i].status == FREE) {
            envoys[i].status = ON_MISSION;
            envoys[i].mission_type = 0;
            free(envoys[i].mission_realm);
            envoys[i].mission_realm = (realm != NULL) ? strdup(realm) : NULL;
            return envoys[i].id;
        }
    }

    return -1;
}

/* release_envoy — Marks the envoy with the given id as FREE. */
void release_envoy(Envoy *envoys, int count, int envoy_id) {
    int i;

    if (envoys == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (envoys[i].id == envoy_id) {
            envoys[i].status = FREE;
            envoys[i].mission_type = 0;
            free(envoys[i].mission_realm);
            envoys[i].mission_realm = NULL;
            return;
        }
    }
}

/* find_envoy_by_id — Returns a pointer to the envoy with the given id, or NULL. */
Envoy *find_envoy_by_id(Envoy *envoys, int count, int id) {
    int i;

    if (envoys == NULL) {
        return NULL;
    }

    for (i = 0; i < count; i++) {
        if (envoys[i].id == id) {
            return &envoys[i];
        }
    }

    return NULL;
}

/* assign_envoy_pledge — Writes a PLEDGE task message to the envoy's pipe. */
int assign_envoy_pledge(Envoy *e, const char *target_ip, int target_port, const char *dest_realm,
                        const char *sender_realm, const char *sender_ip, int sender_port,
                        const char *sigil_path) {
    char *msg = NULL;
    ssize_t n;
    size_t len;

    if (e == NULL || target_ip == NULL || dest_realm == NULL || sender_realm == NULL ||
        sender_ip == NULL || sigil_path == NULL) {
        return -1;
    }

    if (asprintf(&msg, "PLEDGE %s %d %s %s %s %d %s\n", target_ip, target_port, dest_realm,
                 sender_realm, sender_ip, sender_port, sigil_path) < 0) {
        return -1;
    }

    len = strlen(msg);
    n = write(e->fd_write, msg, len);
    free(msg);

    return (n == (ssize_t)len) ? 0 : -1;
}

/* assign_envoy_trade — Writes a TRADE task message (header + raw payload) to the envoy's pipe. */
int assign_envoy_trade(Envoy *e, const char *target_ip, int target_port, const char *dest_realm,
                       const char *sender_realm, const char *sender_ip, int sender_port,
                       const char *payload, size_t payload_len) {
    char *header = NULL;
    ssize_t n;
    size_t hlen;

    if (e == NULL || target_ip == NULL || dest_realm == NULL || sender_realm == NULL ||
        sender_ip == NULL || payload == NULL) {
        return -1;
    }

    if (asprintf(&header, "TRADE %s %d %s %s %s %d %zu\n", target_ip, target_port, dest_realm,
                 sender_realm, sender_ip, sender_port, payload_len) < 0) {
        return -1;
    }

    hlen = strlen(header);
    n = write(e->fd_write, header, hlen);
    free(header);

    if (n != (ssize_t)hlen) {
        return -1;
    }

    /* Write raw payload bytes immediately after the header line */
    n = write(e->fd_write, payload, payload_len);
    return (n == (ssize_t)payload_len) ? 0 : -1;
}

/* assign_envoy_list_products — Writes a LIST PRODUCTS task message to the envoy's pipe. */
int assign_envoy_list_products(Envoy *e, const char *target_ip, int target_port,
                               const char *dest_realm, const char *sender_realm,
                               const char *sender_ip, int sender_port) {
    char *msg = NULL;
    ssize_t n;
    size_t len;

    if (e == NULL || target_ip == NULL || dest_realm == NULL || sender_realm == NULL ||
        sender_ip == NULL) {
        return -1;
    }

    if (asprintf(&msg, "LIST %s %d %s %s %s %d\n", target_ip, target_port, dest_realm, sender_realm,
                 sender_ip, sender_port) < 0) {
        return -1;
    }

    len = strlen(msg);
    n = write(e->fd_write, msg, len);
    free(msg);

    return (n == (ssize_t)len) ? 0 : -1;
}

/* display_envoys — Prints status of all envoys to stdout. */
void display_envoys(Envoy *envoys, int count) {
    char *line = NULL;
    int i;

    if (envoys == NULL) {
        char *msg = NULL;
        if (asprintf(&msg, "No envoys available.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return;
    }

    for (i = 0; i < count; i++) {
        if (envoys[i].status == FREE) {
            asprintf(&line, "- Envoy %d: FREE\n", envoys[i].id);
        } else {
            const char *mr = envoys[i].mission_realm;
            if (mr == NULL) {
                mr = "(unknown)";
            }
            asprintf(&line, "- Envoy %d: ON_MISSION (%s)\n", envoys[i].id, mr);
        }
        if (line != NULL) {
            printF(line);
            free(line);
            line = NULL;
        }
    }
}
