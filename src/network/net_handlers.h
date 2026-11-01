#ifndef NET_HANDLERS_H
#define NET_HANDLERS_H

#include "protocol2.h"
#include "types.h"

void process_incoming_frame(Maester *maester, int client_fd, const Frame *frame);

#endif
