#ifndef ALLIANCE2_H
#define ALLIANCE2_H

typedef enum { PENDING, ON_PENDING, ALLIED, FAILED, REJECTED } AllianceStatus;

typedef struct {
    char *realm;
    char *ip;
    int port;
    AllianceStatus status;
} Alliance;

Alliance *init_alliances(void);
void free_alliances(Alliance *alliances, int count);
int find_alliance_index(Alliance *alliances, int count, const char *realm);
int is_allied(Alliance *alliances, int count, const char *realm);
int upsert_alliance(Alliance **alliances, int *count, const char *realm, const char *ip, int port,
                    AllianceStatus status);

#endif
