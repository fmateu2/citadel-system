CC = gcc
CFLAGS = -Wall -Wextra -g -D_GNU_SOURCE \
	-Isrc/core -Isrc/config -Isrc/state -Isrc/network -Isrc/commands -Isrc/utils

SRCS = src/core/maester.c src/config/config2.c \
	src/state/pledge.c src/state/alliance2.c src/state/envoy2.c src/state/inventory2.c \
	src/network/network2.c src/network/protocol2.c src/network/trade2.c \
	src/network/net_utils.c src/network/net_frame.c \
	src/network/net_transfer.c src/network/net_handlers.c \
	src/commands/commands2.c \
	src/utils/utils2.c

OBJS = $(SRCS:.c=.o)

all: Maester

Maester: $(OBJS)
	$(CC) $(CFLAGS) -o Maester $(OBJS) -pthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

westeros:
	@echo "Peace"

clean:
	rm -f $(OBJS) Maester
	find src/ -name "*.o" -delete

.PHONY: all clean westeros
