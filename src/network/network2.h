#ifndef NETWORK2_H
#define NETWORK2_H

#include "types.h"

int start_listener(Maester *maester);
void stop_listener(Maester *maester);

int send_pledge_response(Maester *maester, unsigned long request_id, const char *realm,
                         const char *ip, int port, const char *decision);

int send_list_products_request(Maester *maester, const char *realm);
int send_trade_order(Maester *maester, const char *realm, const char *payload);

void notify_allied_realms_exit(Maester *maester);
void check_pending_timeouts(Maester *maester);

#endif
