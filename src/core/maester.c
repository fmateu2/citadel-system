/* maester.c — Application entry point: initialization, main loop, and cleanup. */
#include "types.h"
#include "pledge.h"
#include "commands2.h"
#include "network2.h"
#include "utils2.h"
#include "config2.h"
#include "inventory2.h"
#include "envoy2.h"
#include "alliance2.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>
#include <pthread.h>

static int *g_keep_running = NULL;

static void signal_handler(int sig) {
    (void)sig;
    if (g_keep_running != NULL) {
        *g_keep_running = 0;
    }
}

/* load_maester — Reads config and inventory, allocates all subsystems, returns a Maester. */
static Maester load_maester(char *config_path, char *inventory_path) {
    Maester maester;

    memset(&maester, 0, sizeof(Maester));

    maester.config = load_file(config_path);
    if (maester.config.realm == NULL) {
        return maester;
    }

    maester.inventory = load_inventory(inventory_path);
    maester.envoys = load_envoys(maester.config);
    maester.num_envoys = maester.config.num_envoys;
    maester.alliances = init_alliances();
    maester.num_alliances = 0;
    maester.pending_pledges = NULL;
    maester.num_pending_pledges = 0;
    maester.remote_products_text = NULL;
    maester.remote_products_realm = NULL;
    maester.keep_running = 1;
    maester.server_fd = -1;
    maester.next_request_id = 1;

    pthread_mutex_init(&maester.lock, NULL);

    return maester;
}

/* free_maester — Frees all subsystems and zeroes the Maester struct. */
static void free_maester(Maester *maester) {
    int i;

    if (maester == NULL) {
        return;
    }

    for (i = 0; i < maester->num_pending_pledges; i++) {
        free(maester->pending_pledges[i].realm);
        free(maester->pending_pledges[i].ip);
        free(maester->pending_pledges[i].order_text);
    }
    free(maester->pending_pledges);

    free_alliances(maester->alliances, maester->num_alliances);
    free_envoys(maester->envoys, maester->num_envoys);
    free_inventory(&maester->inventory);
    free_config(&maester->config);
    free(maester->remote_products_text);
    free(maester->remote_products_realm);
    free(maester->pending_rx_md5);
    free(maester->pending_rx_realm);
    free(maester->pending_rx_origin_ip);
    free(maester->pending_order_text);

    pthread_mutex_destroy(&maester->lock);

    memset(maester, 0, sizeof(Maester));
}

int main(int argc, char *argv[]) {
    Maester maester;
    char *line;
    char *msg = NULL;
    int needs_prompt = 1;
    struct sigaction sa;

    if (argc < 3) {
        char *usage = NULL;
        if (asprintf(&usage, "Usage: Maester <config.dat> <stock.db>\n") >= 0) {
            printF(usage);
        }
        free(usage);
        return 1;
    }

    maester = load_maester(argv[1], argv[2]);
    if (maester.config.realm == NULL) {
        return 1;
    }
    maester.inventory_path = argv[2];

    g_keep_running = &maester.keep_running;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (start_listener(&maester) != 0) {
        free_maester(&maester);
        char *err = NULL;
        if (asprintf(&err, "ERROR: Could not start listener.\n") >= 0) {
            printF(err);
        }
        free(err);
        return 1;
    }

    asprintf(&msg, "Maester of %s initialized. The board is set.\n", maester.config.realm);
    if (msg != NULL) {
        printF(msg);
        free(msg);
    }

    while (maester.keep_running) {
        fd_set rfds;
        struct timeval tv;
        int sel;
        int maxfd;
        int i;

        check_pending_timeouts(&maester);

        if (needs_prompt) {
            char *prompt = NULL;
            if (asprintf(&prompt, "$ ") >= 0) {
                printF(prompt);
            }
            free(prompt);
            needs_prompt = 0;
        }

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        maxfd = STDIN_FILENO;

        /* Include envoy result pipes in the select set */
        for (i = 0; i < maester.num_envoys; i++) {
            if (maester.envoys[i].status == ON_MISSION && maester.envoys[i].fd_read >= 0) {
                FD_SET(maester.envoys[i].fd_read, &rfds);
                if (maester.envoys[i].fd_read > maxfd) {
                    maxfd = maester.envoys[i].fd_read;
                }
            }
        }

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        /* select() returns -1 on EINTR (signal received); exit the loop cleanly */
        sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            break;
        }
        if (sel == 0) {
            /* Timeout with no input — loop back to check timeouts again */
            continue;
        }

        /* Check if any envoy reported a result */
        for (i = 0; i < maester.num_envoys; i++) {
            if (maester.envoys[i].status == ON_MISSION && maester.envoys[i].fd_read >= 0 &&
                FD_ISSET(maester.envoys[i].fd_read, &rfds)) {
                char result[16];
                ssize_t nr = read(maester.envoys[i].fd_read, result, sizeof(result) - 1);
                if (nr > 0) {
                    result[nr] = '\0';
                    if (strncmp(result, "FAIL", 4) == 0) {
                        /* Envoy could not complete the send; clean up the pending entry */
                        int envoy_id = maester.envoys[i].id;
                        char *err_msg = NULL;
                        pthread_mutex_lock(&maester.lock);
                        int pidx = find_pending_by_envoy_id(&maester, envoy_id);
                        if (pidx >= 0) {
                            const char *failed_realm = maester.pending_pledges[pidx].realm;
                            if (maester.pending_pledges[pidx].type == PENDING_OUTGOING) {
                                upsert_alliance(&maester.alliances, &maester.num_alliances,
                                                failed_realm, NULL, 0, FAILED);
                            }
                            if (asprintf(&err_msg, "\n>>> Envoy %d failed to send to %s.\n$ ",
                                         envoy_id,
                                         failed_realm != NULL ? failed_realm : "(unknown)") < 0) {
                                err_msg = NULL;
                            }
                            remove_pending_pledge_by_request(
                                &maester, maester.pending_pledges[pidx].request_id);
                        }
                        release_envoy(maester.envoys, maester.num_envoys, envoy_id);
                        pthread_mutex_unlock(&maester.lock);
                        if (err_msg != NULL) {
                            printF(err_msg);
                            free(err_msg);
                        }
                    }
                    /* On OK, the envoy has sent the request. It is released when the remote
                     * response arrives. */
                }
            }
        }

        if (!FD_ISSET(STDIN_FILENO, &rfds)) {
            continue;
        }

        line = readUntil(STDIN_FILENO, '\n');
        if (line == NULL) {
            break;
        }

        str_trim(line);
        if (line[0] != '\0' && maester.keep_running) {
            dispatch(&maester, line, &maester.keep_running);
        }

        free(line);
        needs_prompt = 1;
    }

    maester.keep_running = 0;
    notify_allied_realms_exit(&maester);
    stop_listener(&maester);
    free_maester(&maester);

    return 0;
}
