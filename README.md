# The Citadel System

### Independent processes. Structured messages. Coordinated trade.

**Francesc Mateu Vallespín & Jan Francesc Porcar Guillen**  
Operating Systems · La Salle, Universitat Ramon Llull · 2025–2026 · Group 15

**C · POSIX processes · Pipes · pthreads · TCP sockets · Binary protocols**

## Description

Citadel is a decentralized network simulation. Each `Maester` instance represents a realm with its own command terminal, inventory, routing table and alliance state. Realms exchange sigils, establish alliances, request product lists and submit trade orders.

Persistent child processes, called **envoys**, perform outgoing missions. The parent communicates with them through pipes while a background listener accepts TCP connections. Worker threads handle incoming frames, and mutexes coordinate access to shared state.

The repository contains the complete Phase 4 submission: 30 C source/header files, the Makefile, five sample realms and the original technical report. `Maester` is the executable name; **The Citadel System** is the project name. Trading belongs to the fictional simulation, not a financial exchange.

## Build

Use Linux, or Linux through WSL2, with GCC, GNU Make, pthreads and `md5sum`. Python 3 is used only by the optional setup and verification scripts.

```bash
make
```

The Makefile builds `Maester` with `-Wall -Wextra`. The implementation uses POSIX interfaces and GNU extensions such as `asprintf`; it is not a native Windows application.

## Run locally

Create independent copies of the sample inventories and five configurations advertising `127.0.0.1`:

```bash
python3 scripts/prepare_local.py
```

Open two terminals in the project directory.

**Terminal 1 — Stark**

```bash
./Maester .demo/Stark/config.dat .demo/Stark/inventory.db
```

**Terminal 2 — Lannister**

```bash
./Maester .demo/Lannister/config.dat .demo/Lannister/inventory.db
```

The helper uses ports **18890–18894**, creates two envoys per realm and leaves the original `data/` files intact. Use `--base-port` to choose a different port block or `--output` for a new destination. It refuses to overwrite an existing directory.

### Walk through an alliance and trade

1. In Stark, enter `PLEDGE Lannister stark.png`.
2. Wait for Lannister to report the incoming request, then enter `PLEDGE RESPOND Stark ACCEPT` there.
3. In Stark, enter `LIST PRODUCTS Lannister` and wait for the remote list.
4. Enter `START TRADE Lannister`, followed by `ADD Golden Lion Statuette 1` and `SEND`.
5. The accepted order updates both inventories. Enter `EXIT` in each terminal to stop.

Use `LIST PRODUCTS` to inspect local stock, `PLEDGE STATUS` to inspect alliances, and `ENVOY STATUS` to inspect the worker pool. The sample order above is exercised by the included smoke check.

## Commands

| Command                                 | Operation                                                      |
| --------------------------------------- | -------------------------------------------------------------- |
| `LIST REALMS`                           | Display configured realm routes                                |
| `LIST PRODUCTS`                         | Display local inventory                                        |
| `LIST PRODUCTS <Realm>`                 | Request an allied realm's product list                         |
| `PLEDGE <Realm> <Sigil>`                | Send an alliance request and sigil file                        |
| `PLEDGE STATUS`                         | Display alliance states                                        |
| `PLEDGE RESPOND <Realm> ACCEPT\|REJECT` | Answer an incoming request                                     |
| `ENVOY STATUS`                          | Display free and assigned envoys                               |
| `START TRADE <Realm>`                   | Open a trade session using the previously fetched product list |
| `EXIT`                                  | Notify allies and shut down                                    |

Within a trade session, use `ADD <product> <amount>`, `REMOVE <product> <amount>`, `LIST`, `SEND` or `CANCEL`. Both `EXIT` and Ctrl+C follow the shutdown path.

## Configuration

Each `.dat` file contains these fields, in order:

```text
Realm name
Inventory and sigil directory
Number of envoys
Advertised IP address
Listen port
--- ROUTES ---
RealmName IP Port
DEFAULT IP Port
```

The sample files in `data/` use the loopback address `127.0.0.1` and ports 8890–8894. Original lab network addresses are omitted from the public examples. Use the generated local configurations for a first run. For a custom network, copy a sample to a file ending in `.local.dat` before adding your own addresses; these local files are excluded from Git. A route with `*.*.*.*` as its address falls back to the configured default route.

The listener implementation binds `INADDR_ANY`; the configured IP is the address advertised in outgoing frames. Local configurations do not change that binding behavior. Run this educational protocol in a trusted local or lab environment.

## Protocol

Every serialized frame contains exactly **320 bytes**. The serializer writes fields explicitly rather than sending a raw C struct, so the network layout does not depend on struct padding.

| Field       | Offset |      Size | Representation                                      |
| ----------- | -----: | --------: | --------------------------------------------------- |
| Type        |      0 |    1 byte | Message identifier                                  |
| Origin      |      1 |  20 bytes | Null-padded `IP:port`                               |
| Destination |     21 |  20 bytes | Null-padded realm name                              |
| Data length |     41 |   2 bytes | Unsigned integer, network byte order                |
| Data        |     43 | 275 bytes | Payload and zero padding                            |
| Checksum    |    318 |   2 bytes | Sum of bytes 0–317 modulo 65536, network byte order |

`read_full` and `write_full` accumulate partial socket operations until a full frame has been transferred. `parse_frame` checks the checksum and rejects a payload length above 275 bytes.

Files use a header, `ACK_FILE`, data chunks and an `ACK_MD5` response. The header carries the file name, size and MD5 value. MD5 and the frame checksum are integrity mechanisms for the assignment; they do not authenticate peers or encrypt traffic.

## Modules

| Directory       | Responsibility                                                         |
| --------------- | ---------------------------------------------------------------------- |
| `src/core/`     | Initialization, signal handling, command/event loop and cleanup        |
| `src/config/`   | Configuration and route parsing                                        |
| `src/state/`    | Envoy processes, alliance state, pending requests and inventory        |
| `src/network/`  | TCP listener, framing, routing, file transfer and message handlers     |
| `src/commands/` | User command dispatch                                                  |
| `src/utils/`    | String and low-level I/O helpers                                       |
| `data/`         | Local sample configurations, binary inventories and sigils for five realms |
| `scripts/`      | Local setup and repeatable two-realm smoke check                       |
| `docs/`         | Original report, original README and verification transcript           |

The interactive trade sub-loop lives in `src/network/trade2.c`. The frontend's protocol explorer is a separate JavaScript illustration in the portfolio; it does not run these POSIX processes in the browser.

## Verification

```bash
make
python3 scripts/smoke_check.py
```

The smoke check uses temporary inventory copies and available local ports. It launches Stark and Lannister, checks envoy availability, transfers a sigil, accepts the alliance, fetches the remote product list, submits a trade, verifies both persisted stock quantities and checks successful process exits. Temporary data is removed afterward.

Compilation and this two-realm flow passed on Linux with GCC. The captured check transcript is in `docs/verified-run.txt`. This is a functional smoke check; it does not establish reliability under high concurrency, malformed traffic or network failure. Multi-hop routing is included in the source but is outside this check.

## Implementation notes

- The original C logic, names and module boundaries are preserved. Formatting uses four-space indentation, consistent wrapping and the existing comment style.
- Inventory files store native `Product` records (`char[100]`, `int`, `float`). Their portability depends on the host ABI; the network frame has a separate explicit layout.
- Several incoming-transfer fields are shared on the `Maester` structure. More extensive concurrent-transfer and shutdown testing would be a useful next step.
- The report documents the project's development across phases. This README follows the supplied Phase 4 source paths and behavior.
- The local setup script and smoke check are packaging additions. Compiled executables, object files and operating-system metadata are excluded from the source download.

## Clean

```bash
make clean
```

## Authors and reuse

Developed by **Francesc Mateu Vallespín** and **Jan Francesc Porcar Guillen**. The original report is included for technical context and authorship. No new open-source license is assigned by this packaging work.
