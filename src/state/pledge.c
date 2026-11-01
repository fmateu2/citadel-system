/* pledge.c — Manages the pending pledge list on the Maester state. */
#include "pledge.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* add_pending_pledge — Appends a new pledge entry to the dynamic array. */
int add_pending_pledge(Maester *maester, unsigned long request_id, const char *realm,
                       const char *ip, int port, int envoy_id, PendingType type) {
    PendingPledge *tmp;

    tmp = realloc(maester->pending_pledges,
                  sizeof(PendingPledge) * (maester->num_pending_pledges + 1));
    if (tmp == NULL) {
        return -1;
    }

    maester->pending_pledges = tmp;
    maester->pending_pledges[maester->num_pending_pledges].request_id = request_id;
    if (realm != NULL) {
        maester->pending_pledges[maester->num_pending_pledges].realm = strdup(realm);
    } else {
        maester->pending_pledges[maester->num_pending_pledges].realm = NULL;
    }
    if (ip != NULL) {
        maester->pending_pledges[maester->num_pending_pledges].ip = strdup(ip);
    } else {
        maester->pending_pledges[maester->num_pending_pledges].ip = NULL;
    }
    maester->pending_pledges[maester->num_pending_pledges].port = port;
    maester->pending_pledges[maester->num_pending_pledges].envoy_id = envoy_id;
    maester->pending_pledges[maester->num_pending_pledges].type = type;
    maester->pending_pledges[maester->num_pending_pledges].created_at = time(NULL);
    maester->pending_pledges[maester->num_pending_pledges].order_text = NULL;

    if (realm != NULL && maester->pending_pledges[maester->num_pending_pledges].realm == NULL) {
        return -1;
    }

    maester->num_pending_pledges++;
    return 0;
}

/* remove_pending_pledge_by_request — Removes pledge with the given request_id and compacts the
 * array. */
int remove_pending_pledge_by_request(Maester *maester, unsigned long request_id) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].request_id == request_id) {
            int j;

            free(maester->pending_pledges[i].realm);
            free(maester->pending_pledges[i].ip);
            free(maester->pending_pledges[i].order_text);

            for (j = i; j < maester->num_pending_pledges - 1; j++) {
                maester->pending_pledges[j] = maester->pending_pledges[j + 1];
            }

            maester->num_pending_pledges--;

            if (maester->num_pending_pledges == 0) {
                free(maester->pending_pledges);
                maester->pending_pledges = NULL;
            } else {
                PendingPledge *tmp = realloc(maester->pending_pledges,
                                             sizeof(PendingPledge) * maester->num_pending_pledges);
                if (tmp != NULL) {
                    maester->pending_pledges = tmp;
                }
            }

            return 0;
        }
    }

    return -1;
}

/* find_pending_outgoing_by_request — Returns array index of the outgoing pledge with matching
 * request_id, or -1. */
int find_pending_outgoing_by_request(Maester *maester, unsigned long request_id) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].request_id == request_id &&
            maester->pending_pledges[i].type == PENDING_OUTGOING) {
            return i;
        }
    }

    return -1;
}

/* has_pending_outgoing_pledge — Returns 1 if there is any outgoing pledge still pending, 0
 * otherwise. */
int has_pending_outgoing_pledge(Maester *maester) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].type == PENDING_OUTGOING) {
            return 1;
        }
    }

    return 0;
}

/* find_pending_trade_by_realm — Returns array index of the pending trade for the given realm, or
 * -1. */
int find_pending_trade_by_realm(Maester *maester, const char *realm) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].type == PENDING_TRADE &&
            maester->pending_pledges[i].realm != NULL &&
            strcasecmp(maester->pending_pledges[i].realm, realm) == 0) {
            return i;
        }
    }

    return -1;
}

/* find_pending_by_envoy_id — Returns array index of any pending entry assigned to the given envoy,
 * or -1. */
int find_pending_by_envoy_id(Maester *maester, int envoy_id) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].envoy_id == envoy_id) {
            return i;
        }
    }

    return -1;
}

/* find_pending_incoming_by_realm — Returns array index of the incoming pledge from the given realm,
 * or -1. */
int find_pending_incoming_by_realm(Maester *maester, const char *realm) {
    int i;

    for (i = 0; i < maester->num_pending_pledges; i++) {
        if (maester->pending_pledges[i].type == PENDING_INCOMING &&
            maester->pending_pledges[i].realm != NULL &&
            strcasecmp(maester->pending_pledges[i].realm, realm) == 0) {
            return i;
        }
    }

    return -1;
}
