/* alliance2.c — Manages the list of known realms and their alliance status. */
#include "alliance2.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* init_alliances — Returns an empty alliance list (NULL). */
Alliance *init_alliances(void) {
    return NULL;
}

/* free_alliances — Frees all alliance entries and the array itself. */
void free_alliances(Alliance *alliances, int count) {
    int i;

    if (alliances == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        free(alliances[i].realm);
        free(alliances[i].ip);
    }

    free(alliances);
}

/* find_alliance_index — Returns the index of the alliance with the given realm name, or -1. */
int find_alliance_index(Alliance *alliances, int count, const char *realm) {
    int i;

    if (alliances == NULL || realm == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (strcasecmp(alliances[i].realm, realm) == 0) {
            return i;
        }
    }

    return -1;
}

/* is_allied — Returns 1 if the given realm has ALLIED status, 0 otherwise. */
int is_allied(Alliance *alliances, int count, const char *realm) {
    int idx = find_alliance_index(alliances, count, realm);

    if (idx < 0) {
        return 0;
    }

    return alliances[idx].status == ALLIED;
}

/* upsert_alliance — Updates an existing alliance entry or inserts a new one. */
int upsert_alliance(Alliance **alliances, int *count, const char *realm, const char *ip, int port,
                    AllianceStatus status) {
    int idx;
    Alliance *tmp;

    if (alliances == NULL || count == NULL || realm == NULL) {
        return -1;
    }

    idx = find_alliance_index(*alliances, *count, realm);
    if (idx >= 0) {
        free((*alliances)[idx].ip);
        if (ip != NULL) {
            (*alliances)[idx].ip = strdup(ip);
        } else {
            (*alliances)[idx].ip = NULL;
        }
        (*alliances)[idx].port = port;
        (*alliances)[idx].status = status;
        return 0;
    }

    tmp = realloc(*alliances, sizeof(Alliance) * (*count + 1));
    if (tmp == NULL) {
        return -1;
    }

    *alliances = tmp;
    (*alliances)[*count].realm = strdup(realm);
    if (ip != NULL) {
        (*alliances)[*count].ip = strdup(ip);
    } else {
        (*alliances)[*count].ip = NULL;
    }
    (*alliances)[*count].port = port;
    (*alliances)[*count].status = status;

    if ((*alliances)[*count].realm == NULL) {
        free((*alliances)[*count].ip);
        (*alliances)[*count].ip = NULL;
        return -1;
    }

    (*count)++;
    return 0;
}
