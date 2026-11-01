/* network2.c — Public API: listener lifecycle, pledge, product list, trade order, and timeouts. */
#include "network2.h"
#include "net_handlers.h"
#include "net_transfer.h"
#include "net_frame.h"
#include "net_utils.h"
#include "protocol2.h"
#include "pledge.h"
#include "utils2.h"
#include "config2.h"
#include "alliance2.h"
#include "envoy2.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PLEDGE_TIMEOUT_SECONDS 120

typedef struct {
    Maester *maester;
    int client_fd;
} ClientContext;

static void *client_thread_main(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    Frame frame;

    if (ctx == NULL) {
        return NULL;
    }

    if (receive_frame_on_fd(ctx->client_fd, &frame) != 0) {
        Frame nack;
        if (prepare_ack_frame(&nack, TYPE_NACK, ctx->maester->config.realm) == 0) {
            send_frame_on_fd(ctx->client_fd, &nack);
        }
        close(ctx->client_fd);
        free(ctx);
        return NULL;
    }

    process_incoming_frame(ctx->maester, ctx->client_fd, &frame);

    close(ctx->client_fd);
    free(ctx);
    return NULL;
}

static void *listener_main(void *arg) {
    Maester *maester = (Maester *)arg;

    while (maester->keep_running) {
        int client_fd;
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        ClientContext *ctx;
        pthread_t thread_id;
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(maester->server_fd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(maester->server_fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            continue;
        }

        client_fd = accept(maester->server_fd, (struct sockaddr *)&peer_addr, &peer_len);
        if (client_fd < 0) {
            if (!maester->keep_running) {
                break;
            }
            continue;
        }

        ctx = malloc(sizeof(ClientContext));
        if (ctx == NULL) {
            close(client_fd);
            continue;
        }

        ctx->maester = maester;
        ctx->client_fd = client_fd;

        if (pthread_create(&thread_id, NULL, client_thread_main, ctx) != 0) {
            close(client_fd);
            free(ctx);
            continue;
        }

        pthread_detach(thread_id);
    }

    return NULL;
}

int start_listener(Maester *maester) {
    int opt = 1;
    struct sockaddr_in addr;

    if (maester == NULL) {
        return -1;
    }

    maester->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (maester->server_fd < 0) {
        return -1;
    }

    setsockopt(maester->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)maester->config.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(maester->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(maester->server_fd);
        maester->server_fd = -1;
        return -1;
    }

    if (listen(maester->server_fd, 10) < 0) {
        close(maester->server_fd);
        maester->server_fd = -1;
        return -1;
    }

    if (pthread_create(&maester->listener_thread, NULL, listener_main, maester) != 0) {
        close(maester->server_fd);
        maester->server_fd = -1;
        return -1;
    }

    return 0;
}

void stop_listener(Maester *maester) {
    if (maester == NULL) {
        return;
    }

    if (maester->server_fd >= 0) {
        close(maester->server_fd);
        maester->server_fd = -1;
    }

    pthread_join(maester->listener_thread, NULL);
}

int send_pledge_response(Maester *maester, unsigned long request_id, const char *realm,
                         const char *ip, int port, const char *decision) {
    Frame frame;
    char *payload = NULL;
    int rc;

    (void)request_id;

    if (maester == NULL || realm == NULL || ip == NULL || port <= 0 || decision == NULL) {
        return -1;
    }

    if (asprintf(&payload, "%s&%s", decision, maester->config.realm) < 0) {
        return -1;
    }

    if (prepare_frame(&frame, TYPE_ALLIANCE_RESPONSE, maester->config.ip, maester->config.port,
                      realm, payload, strlen(payload)) != 0) {
        free(payload);
        return -1;
    }

    free(payload);
    /* Send directly to the requester's IP:port (known from the PLEDGE origin field).
       This ensures the response reaches the originator even when it arrived via hops
       and intermediate nodes don't know how to route back to it. */
    rc = send_one_way_to_ip_port(ip, port, &frame);
    if (rc != 0) {
        rc = send_frame_to_realm(maester, realm, &frame);
    }
    return rc;
}

int send_list_products_request(Maester *maester, const char *realm) {
    Frame frame;

    if (maester == NULL || realm == NULL) {
        return -1;
    }

    if (prepare_frame(&frame, TYPE_PRODUCT_LIST_REQUEST, maester->config.ip, maester->config.port,
                      realm, maester->config.realm, strlen(maester->config.realm)) != 0) {
        return -1;
    }

    return send_frame_to_realm(maester, realm, &frame);
}

int send_trade_order(Maester *maester, const char *realm, const char *payload) {
    int envoy_id;
    Envoy *envoy;
    int idx;
    const char *target_ip = NULL;
    int target_port = 0;
    char *ip_copy = NULL;
    unsigned long request_id;
    size_t payload_len;

    if (maester == NULL || realm == NULL || payload == NULL) {
        return -1;
    }

    payload_len = strlen(payload);

    /* Acquire a free envoy for the trade mission */
    pthread_mutex_lock(&maester->lock);
    envoy_id = acquire_free_envoy(maester->envoys, maester->num_envoys, realm);
    pthread_mutex_unlock(&maester->lock);

    if (envoy_id < 0) {
        return -1;
    }

    envoy = find_envoy_by_id(maester->envoys, maester->num_envoys, envoy_id);
    if (envoy != NULL) {
        envoy->mission_type = 3;
    }

    /* Resolve target IP:port from the existing alliance (trade requires ALLIED status) */
    pthread_mutex_lock(&maester->lock);
    idx = find_alliance_index(maester->alliances, maester->num_alliances, realm);
    if (idx >= 0 && maester->alliances[idx].status == ALLIED &&
        maester->alliances[idx].ip != NULL && maester->alliances[idx].port > 0) {
        target_ip = maester->alliances[idx].ip;
        target_port = maester->alliances[idx].port;
    } else {
        Route *route = find_route(&maester->config, realm);
        if (route != NULL) {
            target_ip = route->ip;
            target_port = route->port;
        }
    }

    if (target_ip == NULL || target_port <= 0) {
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        return -1;
    }

    ip_copy = strdup(target_ip);

    request_id = maester->next_request_id++;
    add_pending_pledge(maester, request_id, realm, ip_copy, target_port, envoy_id, PENDING_TRADE);
    {
        int pidx = find_pending_trade_by_realm(maester, realm);
        if (pidx >= 0) {
            free(maester->pending_pledges[pidx].order_text);
            maester->pending_pledges[pidx].order_text = strdup(payload);
        }
    }
    pthread_mutex_unlock(&maester->lock);

    if (assign_envoy_trade(envoy, ip_copy, target_port, realm, maester->config.realm,
                           maester->config.ip, maester->config.port, payload, payload_len) != 0) {
        pthread_mutex_lock(&maester->lock);
        remove_pending_pledge_by_request(maester, request_id);
        release_envoy(maester->envoys, maester->num_envoys, envoy_id);
        pthread_mutex_unlock(&maester->lock);
        free(ip_copy);
        return -1;
    }

    free(ip_copy);
    return 0;
}

void notify_allied_realms_exit(Maester *maester) {
    int i;

    if (maester == NULL) {
        return;
    }

    for (i = 0; i < maester->num_alliances; i++) {
        Frame frame;

        if (maester->alliances[i].status != ALLIED) {
            continue;
        }

        if (prepare_frame(&frame, TYPE_DISCONNECT, maester->config.ip, maester->config.port,
                          maester->alliances[i].realm, maester->config.realm,
                          strlen(maester->config.realm)) == 0) {
            send_frame_to_realm(maester, maester->alliances[i].realm, &frame);
        }
    }
}

void check_pending_timeouts(Maester *maester) {
    time_t now;
    int i = 0;

    if (maester == NULL) {
        return;
    }

    now = time(NULL);

    pthread_mutex_lock(&maester->lock);
    while (i < maester->num_pending_pledges) {
        if ((maester->pending_pledges[i].type == PENDING_OUTGOING ||
             maester->pending_pledges[i].type == PENDING_PRODUCTS ||
             maester->pending_pledges[i].type == PENDING_TRADE) &&
            difftime(now, maester->pending_pledges[i].created_at) > PLEDGE_TIMEOUT_SECONDS) {
            char *line = NULL;
            const char *realm = maester->pending_pledges[i].realm;

            if (maester->pending_pledges[i].type == PENDING_OUTGOING && realm != NULL) {
                upsert_alliance(&maester->alliances, &maester->num_alliances, realm,
                                maester->pending_pledges[i].ip, maester->pending_pledges[i].port,
                                FAILED);
                if (asprintf(&line, "\n>>> Pledge to %s has failed (TIMEOUT).\n$ ", realm) >= 0) {
                    printF(line);
                }
            } else if (maester->pending_pledges[i].type == PENDING_PRODUCTS && realm != NULL) {
                if (asprintf(&line, "\n>>> Product list request to %s has failed (TIMEOUT).\n$ ",
                             realm) >= 0) {
                    printF(line);
                }
            } else if (maester->pending_pledges[i].type == PENDING_TRADE && realm != NULL) {
                if (asprintf(&line, "\n>>> Trade with %s has failed (TIMEOUT).\n$ ", realm) >= 0) {
                    printF(line);
                }
            }
            free(line);

            if (maester->pending_pledges[i].envoy_id >= 0) {
                release_envoy(maester->envoys, maester->num_envoys,
                              maester->pending_pledges[i].envoy_id);
            }
            remove_pending_pledge_by_request(maester, maester->pending_pledges[i].request_id);
            continue;
        }
        i++;
    }
    pthread_mutex_unlock(&maester->lock);
}
