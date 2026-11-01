#ifndef ENVOY2_H
#define ENVOY2_H

#include "config2.h"
#include <sys/types.h>
#include <stddef.h>

typedef enum { FREE, ON_MISSION } EnvoyStatus;

typedef struct {
    int id;
    EnvoyStatus status;
    char *mission_realm;
    int mission_type; /* 1=PLEDGE, 2=LIST_PRODUCTS, 3=TRADE */
    pid_t pid;
    int fd_write; /* Maester writes here, Envoy reads */
    int fd_read;  /* Maester reads here, Envoy writes */
} Envoy;

Envoy *load_envoys(Config cfg);
void free_envoys(Envoy *envoys, int count);
int acquire_free_envoy(Envoy *envoys, int count, const char *realm);
void release_envoy(Envoy *envoys, int count, int envoy_id);
void display_envoys(Envoy *envoys, int count);
Envoy *find_envoy_by_id(Envoy *envoys, int count, int id);

int assign_envoy_pledge(Envoy *e, const char *target_ip, int target_port, const char *dest_realm,
                        const char *sender_realm, const char *sender_ip, int sender_port,
                        const char *sigil_path);
int assign_envoy_trade(Envoy *e, const char *target_ip, int target_port, const char *dest_realm,
                       const char *sender_realm, const char *sender_ip, int sender_port,
                       const char *payload, size_t payload_len);
int assign_envoy_list_products(Envoy *e, const char *target_ip, int target_port,
                               const char *dest_realm, const char *sender_realm,
                               const char *sender_ip, int sender_port);

#endif
