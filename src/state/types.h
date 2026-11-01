#ifndef TYPES_H
#define TYPES_H

#include <pthread.h>
#include <time.h>

#include "config2.h"
#include "inventory2.h"
#include "envoy2.h"
#include "alliance2.h"

typedef enum { PENDING_OUTGOING, PENDING_INCOMING, PENDING_PRODUCTS, PENDING_TRADE } PendingType;

typedef struct {
    unsigned long request_id;
    char *realm;
    char *ip;
    int port;
    int envoy_id;
    PendingType type;
    time_t created_at;
    char *order_text;
} PendingPledge;

typedef struct {
    Config config;
    Inventory inventory;
    Envoy *envoys;
    int num_envoys;
    Alliance *alliances;
    int num_alliances;
    PendingPledge *pending_pledges;
    int num_pending_pledges;
    char *remote_products_text;
    char *remote_products_realm;
    int keep_running;
    int server_fd;
    pthread_t listener_thread;
    pthread_mutex_t lock;
    unsigned long next_request_id;
    /* Incoming file transfer state (set by header handler, consumed by data handler) */
    size_t pending_rx_size;
    char *pending_rx_md5;
    char *pending_rx_realm;
    char *pending_rx_origin_ip;
    int pending_rx_origin_port;
    int pending_rx_kind; /* 0=sigil  1=products  2=order */
    /* Last sent order payload, used to update requester stock on ACCEPTED */
    char *pending_order_text;
    /* Path to stock.db for persistent saves */
    char *inventory_path;
} Maester;

#endif
