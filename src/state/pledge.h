#ifndef PLEDGE_H
#define PLEDGE_H

#include "types.h"

int add_pending_pledge(Maester *maester, unsigned long request_id, const char *realm,
                       const char *ip, int port, int envoy_id, PendingType type);

int remove_pending_pledge_by_request(Maester *maester, unsigned long request_id);
int find_pending_outgoing_by_request(Maester *maester, unsigned long request_id);
int find_pending_incoming_by_realm(Maester *maester, const char *realm);
int find_pending_trade_by_realm(Maester *maester, const char *realm);
int find_pending_by_envoy_id(Maester *maester, int envoy_id);
int has_pending_outgoing_pledge(Maester *maester);

#endif
