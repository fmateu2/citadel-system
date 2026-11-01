/* commands2.c — Command dispatcher: parses user input and calls the appropriate handler. */
#include "commands2.h"
#include "pledge.h"
#include "trade2.h"
#include "network2.h"
#include "utils2.h"
#include "envoy2.h"
#include "alliance2.h"
#include "config2.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

/* print_unknown — Prints the "Unknown command" error message. */
static void print_unknown(void) {
    char *msg = NULL;
    if (asprintf(&msg, "Unknown command\n") >= 0) {
        printF(msg);
    }
    free(msg);
}

/* handle_exit — Prints sign-off message and sets keep_running to 0. */
static void handle_exit(Maester *maester, int *keep_running) {
    char *buffer = NULL;

    asprintf(&buffer, "The Maester of %s signs off.\nThe ravens rest.\n", maester->config.realm);
    if (buffer != NULL) {
        printF(buffer);
        free(buffer);
    }

    *keep_running = 0;
}

/* handle_list_realms — Prints all configured realm routes. */
static void handle_list_realms(Maester *maester) {
    char *buffer = NULL;
    int i;

    for (i = 0; i < maester->config.num_routes; i++) {
        asprintf(&buffer, "- %s\n", maester->config.routes[i].name);
        if (buffer != NULL) {
            printF(buffer);
            free(buffer);
            buffer = NULL;
        }
    }
}

/* handle_list_products — Displays local inventory or dispatches a remote product-list mission. */
static void handle_list_products(Maester *maester, char *realm) {
    int envoy_id;
    Envoy *envoy;
    const char *target_ip = NULL;
    int target_port = 0;
    int idx;
    Route *route;
    char *ip_copy;

    if (realm == NULL) {
        display_inventory(maester->inventory);
        return;
    }

    if (!is_allied(maester->alliances, maester->num_alliances, realm)) {
        char *line = NULL;
        if (asprintf(&line, "The gates of commerce with %s remain closed; no alliance binds you.\n",
                     realm) >= 0) {
            printF(line);
        }
        free(line);
        return;
    }

    pthread_mutex_lock(&maester->lock);
    envoy_id = acquire_free_envoy(maester->envoys, maester->num_envoys, realm);
    if (envoy_id >= 0) {
        idx = find_alliance_index(maester->alliances, maester->num_alliances, realm);
        if (idx >= 0 && maester->alliances[idx].status == ALLIED &&
            maester->alliances[idx].ip != NULL && maester->alliances[idx].port > 0) {
            target_ip = maester->alliances[idx].ip;
            target_port = maester->alliances[idx].port;
        } else {
            route = find_route(&maester->config, realm);
            if (route != NULL) {
                target_ip = route->ip;
                target_port = route->port;
            }
        }
    }
    pthread_mutex_unlock(&maester->lock);

    if (envoy_id < 0) {
        char *line = NULL;
        if (asprintf(&line, "All envoys are occupied. Your command must wait.\n") >= 0) {
            printF(line);
        }
        free(line);
        return;
    }

    if (target_ip == NULL || target_port <= 0) {
        pthread_mutex_lock(&maester->lock);
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        printF("ERROR: Could not contact target realm.\n");
        return;
    }

    envoy = find_envoy_by_id(maester->envoys, maester->num_envoys, envoy_id);
    if (envoy != NULL) {
        envoy->mission_type = 2;
    }
    ip_copy = strdup(target_ip);
    if (ip_copy == NULL) {
        pthread_mutex_lock(&maester->lock);
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        printF("ERROR: Could not dispatch product-list mission.\n");
        return;
    }

    pthread_mutex_lock(&maester->lock);
    add_pending_pledge(maester, maester->next_request_id++, realm, ip_copy, target_port, envoy_id,
                       PENDING_PRODUCTS);
    pthread_mutex_unlock(&maester->lock);

    if (envoy == NULL ||
        assign_envoy_list_products(envoy, ip_copy, target_port, realm, maester->config.realm,
                                   maester->config.ip, maester->config.port) != 0) {
        pthread_mutex_lock(&maester->lock);
        {
            int pidx = find_pending_by_envoy_id(maester, envoy_id);
            if (pidx >= 0) {
                remove_pending_pledge_by_request(maester,
                                                 maester->pending_pledges[pidx].request_id);
            }
        }
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        free(ip_copy);
        printF("ERROR: Could not dispatch product-list mission.\n");
        return;
    }

    free(ip_copy);
}

/* handle_pledge — Acquires a free envoy and dispatches the pledge mission to it. */
static void handle_pledge(Maester *maester, char *realm, char *sigil) {
    char *buffer = NULL;
    char *full_sigil_path = NULL;
    int fd;
    int envoy_id;
    Envoy *envoy;
    const char *target_ip = NULL;
    int target_port = 0;
    char *ip_copy = NULL;
    unsigned long request_id;
    Route *route;
    int idx;

    if (realm == NULL || sigil == NULL) {
        char *msg = NULL;
        if (asprintf(&msg, "Did you mean to send a pledge? Please review syntax.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        return;
    }

    /* Build full path using config.path */
    if (maester->config.path != NULL && strcmp(maester->config.path, "") != 0) {
        if (asprintf(&full_sigil_path, "%s/%s", maester->config.path, sigil) < 0) {
            return;
        }
    } else {
        full_sigil_path = strdup(sigil);
    }
    if (full_sigil_path == NULL) {
        return;
    }

    /* Check sigil file exists */
    fd = open(full_sigil_path, O_RDONLY);
    if (fd < 0) {
        char *msg = NULL;
        if (asprintf(&msg, "Sigil file not found. The pledge is hereby withdrawn.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        free(full_sigil_path);
        return;
    }
    close(fd);

    /* Acquire a free envoy for this mission */
    pthread_mutex_lock(&maester->lock);
    envoy_id = acquire_free_envoy(maester->envoys, maester->num_envoys, realm);
    pthread_mutex_unlock(&maester->lock);

    if (envoy_id < 0) {
        char *msg = NULL;
        if (asprintf(&msg, "No envoy is available. The pledge is hereby withdrawn.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        free(full_sigil_path);
        return;
    }

    envoy = find_envoy_by_id(maester->envoys, maester->num_envoys, envoy_id);
    if (envoy != NULL) {
        envoy->mission_type = 1;
    }

    /* Resolve target IP:port — prefer allied route, fall back to config route */
    pthread_mutex_lock(&maester->lock);
    idx = find_alliance_index(maester->alliances, maester->num_alliances, realm);
    if (idx >= 0 && maester->alliances[idx].status == ALLIED &&
        maester->alliances[idx].ip != NULL && maester->alliances[idx].port > 0) {
        target_ip = maester->alliances[idx].ip;
        target_port = maester->alliances[idx].port;
    } else {
        route = find_route(&maester->config, realm);
        if (route != NULL) {
            target_ip = route->ip;
            target_port = route->port;
        }
    }

    if (target_ip == NULL || target_port <= 0) {
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        char *msg = NULL;
        if (asprintf(&msg, "No such realm exists. The pledge is hereby withdrawn.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        free(full_sigil_path);
        return;
    }

    ip_copy = strdup(target_ip);

    request_id = maester->next_request_id++;
    add_pending_pledge(maester, request_id, realm, ip_copy, target_port, envoy_id,
                       PENDING_OUTGOING);
    upsert_alliance(&maester->alliances, &maester->num_alliances, realm, ip_copy, target_port,
                    PENDING);
    pthread_mutex_unlock(&maester->lock);

    if (assign_envoy_pledge(envoy, ip_copy, target_port, realm, maester->config.realm,
                            maester->config.ip, maester->config.port, full_sigil_path) != 0) {
        pthread_mutex_lock(&maester->lock);
        remove_pending_pledge_by_request(maester, request_id);
        upsert_alliance(&maester->alliances, &maester->num_alliances, realm, ip_copy, target_port,
                        FAILED);
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        free(ip_copy);
        free(full_sigil_path);
        char *msg = NULL;
        if (asprintf(&msg, "ERROR: Could not dispatch pledge to envoy.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        return;
    }

    free(ip_copy);
    free(full_sigil_path);

    asprintf(&buffer, "Pledge dispatched to Envoy %d for %s.\n", envoy_id, realm);
    if (buffer != NULL) {
        printF(buffer);
        free(buffer);
    }
}

/* handle_pledge_status — Prints the status of every known alliance. */
static void handle_pledge_status(Maester *maester) {
    int i;
    char *line = NULL;

    if (maester->num_alliances == 0) {
        char *msg = NULL;
        if (asprintf(&msg, "You have no pledges awaiting or accepted.\n") >= 0) {
            printF(msg);
        }
        free(msg);
        return;
    }

    for (i = 0; i < maester->num_alliances; i++) {
        const char *status_str = "PENDING";
        switch (maester->alliances[i].status) {
        case PENDING:
            status_str = "PENDING";
            break;
        case ON_PENDING:
            status_str = "ON_PENDING";
            break;
        case ALLIED:
            status_str = "ACCEPTED";
            break;
        case FAILED:
            status_str = "FAILED";
            break;
        case REJECTED:
            status_str = "REJECTED";
            break;
        default:
            break;
        }
        if (asprintf(&line, "- %s: %s\n", maester->alliances[i].realm, status_str) >= 0) {
            printF(line);
        }
        free(line);
        line = NULL;
    }
}

/* handle_pledge_respond — Sends ACCEPT or REJECT in response to an incoming pledge. */
static void handle_pledge_respond(Maester *maester, char *realm, char *decision) {
    int idx;
    int accepted;
    char *buffer = NULL;
    unsigned long request_id;
    char *target_realm;
    char *target_ip;
    int target_port;

    idx = find_pending_incoming_by_realm(maester, realm);
    if (idx < 0) {
        char *msg = NULL;
        if (asprintf(&msg, "No pending alliance request from that realm.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        return;
    }

    accepted = strcasecmp(decision, "ACCEPT") == 0;
    if (!accepted && strcasecmp(decision, "REJECT") != 0) {
        char *msg = NULL;
        if (asprintf(&msg, "Decision must be ACCEPT or REJECT.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        return;
    }

    pthread_mutex_lock(&maester->lock);

    request_id = maester->pending_pledges[idx].request_id;
    target_realm = strdup(maester->pending_pledges[idx].realm);
    if (maester->pending_pledges[idx].ip != NULL) {
        target_ip = strdup(maester->pending_pledges[idx].ip);
    } else {
        target_ip = NULL;
    }
    target_port = maester->pending_pledges[idx].port;

    pthread_mutex_unlock(&maester->lock);

    if (target_realm == NULL || target_ip == NULL) {
        free(target_realm);
        free(target_ip);
        char *msg = NULL;
        if (asprintf(&msg, "ERROR: Incomplete pending alliance data.\n") >= 0) {
            printF(msg);
            free(msg);
        }
        return;
    }

    const char *decision_str;
    if (accepted) {
        decision_str = "ACCEPT";
    } else {
        decision_str = "REJECT";
    }

    if (send_pledge_response(maester, request_id, target_realm, target_ip, target_port,
                             decision_str) == 0) {
        pthread_mutex_lock(&maester->lock);
        AllianceStatus new_status;
        if (accepted) {
            new_status = ALLIED;
        } else {
            new_status = REJECTED;
        }
        upsert_alliance(&maester->alliances, &maester->num_alliances, target_realm, target_ip,
                        target_port, new_status);
        remove_pending_pledge_by_request(maester, request_id);
        pthread_mutex_unlock(&maester->lock);

        const char *status_str;
        if (accepted) {
            status_str = "established";
        } else {
            status_str = "rejected";
        }
        asprintf(&buffer, "Alliance with %s %s.\n", target_realm, status_str);
        if (buffer != NULL) {
            printF(buffer);
            free(buffer);
        }
    } else {
        char *msg = NULL;
        if (asprintf(&msg, "ERROR: Could not send alliance response.\n") >= 0) {
            printF(msg);
            free(msg);
        }
    }

    free(target_realm);
    free(target_ip);
}

/* dispatch — Parses a command line and calls the appropriate handler function. */
void dispatch(Maester *maester, char *line, int *keep_running) {
    char *copy;
    char *token;
    char *arg1;
    char *arg2;

    check_pending_timeouts(maester);

    copy = strdup(line);
    if (copy == NULL) {
        return;
    }

    token = strtok(copy, " ");
    if (token == NULL) {
        free(copy);
        print_unknown();
        return;
    }

    if (strcasecmp(token, "EXIT") == 0) {
        if (strtok(NULL, " ") != NULL) {
            print_unknown();
        } else {
            handle_exit(maester, keep_running);
        }
        free(copy);
        return;
    }

    if (strcasecmp(token, "LIST") == 0) {
        arg1 = strtok(NULL, " ");
        if (arg1 == NULL) {
            char *msg = NULL;
            if (asprintf(&msg,
                         "Missing arguments.\nDid you mean LIST REALMS or LIST PRODUCTS?\n") >= 0) {
                printF(msg);
                free(msg);
            }
        } else if (strcasecmp(arg1, "REALMS") == 0) {
            if (strtok(NULL, " ") != NULL) {
                print_unknown();
            } else {
                handle_list_realms(maester);
            }
        } else if (strcasecmp(arg1, "PRODUCTS") == 0) {
            arg2 = strtok(NULL, " ");
            if (strtok(NULL, " ") != NULL) {
                print_unknown();
            } else {
                handle_list_products(maester, arg2);
            }
        } else {
            print_unknown();
        }

        free(copy);
        return;
    }

    if (strcasecmp(token, "PLEDGE") == 0) {
        arg1 = strtok(NULL, " ");
        if (arg1 == NULL) {
            char *msg = NULL;
            if (asprintf(&msg, "Did you mean to send a pledge? Please review syntax.\n") >= 0) {
                printF(msg);
                free(msg);
            }
        } else if (strcasecmp(arg1, "RESPOND") == 0) {
            char *realm = strtok(NULL, " ");
            char *dec = strtok(NULL, " ");

            if (realm == NULL || dec == NULL || strtok(NULL, " ") != NULL) {
                char *msg = NULL;
                if (asprintf(&msg,
                             "Did you mean to respond to a pledge? Please review syntax.\n") >= 0) {
                    printF(msg);
                    free(msg);
                }
            } else {
                handle_pledge_respond(maester, realm, dec);
            }
        } else if (strcasecmp(arg1, "STATUS") == 0) {
            if (strtok(NULL, " ") != NULL) {
                char *msg = NULL;
                if (asprintf(&msg, "Usage: PLEDGE STATUS\n") >= 0) {
                    printF(msg);
                    free(msg);
                }
            } else {
                handle_pledge_status(maester);
            }
        } else {
            char *sigil = strtok(NULL, " ");
            if (sigil == NULL || strtok(NULL, " ") != NULL) {
                char *msg = NULL;
                if (asprintf(&msg, "Did you mean to send a pledge? Please review syntax.\n") >= 0) {
                    printF(msg);
                    free(msg);
                }
            } else {
                handle_pledge(maester, arg1, sigil);
            }
        }

        free(copy);
        return;
    }

    if (strcasecmp(token, "START") == 0) {
        arg1 = strtok(NULL, " ");
        arg2 = strtok(NULL, " ");

        if (arg1 == NULL || strcasecmp(arg1, "TRADE") != 0 || arg2 == NULL ||
            strtok(NULL, " ") != NULL) {
            char *msg = NULL;
            if (asprintf(&msg,
                         "Missing arguments, can't start a trade.\nPlease review the syntax.\n") >=
                0) {
                printF(msg);
                free(msg);
            }
        } else {
            int check_id;
            pthread_mutex_lock(&maester->lock);
            check_id = acquire_free_envoy(maester->envoys, maester->num_envoys, arg2);
            if (check_id >= 0) {
                release_envoy(maester->envoys, maester->num_envoys, check_id);
            }
            pthread_mutex_unlock(&maester->lock);
            if (check_id < 0) {
                char *msg = NULL;
                if (asprintf(&msg, "No envoy available. Cannot start trade.\n") >= 0) {
                    printF(msg);
                    free(msg);
                }
            } else {
                start_trade_session(maester, arg2, keep_running);
            }
        }

        free(copy);
        return;
    }

    if (strcasecmp(token, "ENVOY") == 0) {
        arg1 = strtok(NULL, " ");
        if (arg1 == NULL || strcasecmp(arg1, "STATUS") != 0 || strtok(NULL, " ") != NULL) {
            char *msg = NULL;
            if (asprintf(&msg, "Missing arguments.\nDid you mean ENVOY STATUS?\n") >= 0) {
                printF(msg);
                free(msg);
            }
        } else {
            display_envoys(maester->envoys, maester->num_envoys);
        }

        free(copy);
        return;
    }

    print_unknown();
    free(copy);
}
