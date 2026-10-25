# Cloud Services Billing Engine

INET 3101 capstone project: a multi-file C billing engine for a fictional
cloud provider. Mock cloud machines report resource usage over TCP, and the
server records it, prices it, and produces invoices.

## Problem Statement

Cloud providers charge customers for what they use: processor and memory
time, storage, and network traffic. This project is a small billing engine
for a fictional cloud provider (assignment Option 2, Cloud Services Billing
Engine).

Mock usage clients send usage events to a billing server over TCP. The
server checks each event, records it in an append-only ledger on disk,
prices the usage with tiered rates modeled on public AWS-style prices, and
produces an invoice for any account and time period.

### What it meters

All money is stored as whole micro-dollars (1 USD = 1,000,000), never as a
floating point number.

| Resource | Client reports | How it is billed |
|---|---|---|
| Compute CPU | vCPU-seconds | 40,000 micro-dollars per vCPU-hour |
| Compute RAM | GB-seconds | 5,000 micro-dollars per GB-hour |
| Storage | MB stored | Rounded down to whole GB. First 100 GB at 23,000 per GB, above that 20,000 per GB. Billed at the peak value reported in the invoice period. |
| Network in | MB | Free |
| Network out | MB | First 1,000 MB free, the next 10,000 MB at 90 per MB, above that 85 per MB |

Each line is rounded half-up to the nearest micro-dollar, and the amount a
customer is charged is the invoice total rounded half-up to a whole cent.
Free allowances and tier thresholds apply once per invoice period. The tier
thresholds are scaled down from real provider values so a demo can show them.

### Features, mapped to the assignment

- **Account and Meter structs:** fixed-size records in `data/accounts.dat`
  and `data/meters.dat`, with create, read, update and delete.
- **Usage ledger ADT:** a hidden struct behind `ledger.h`, in memory and on
  disk.
- **Append-only transaction logging:** every usage event is one line in
  `data/ledger.log`, never edited or removed.
- **Mock usage ingestion server over TCP sockets:** `billing_server` and the
  mock agent `billing_client`.
- **Pricing tier calculations:** `pricing.c`.
- **Invoice generation:** `invoice.c` writes an invoice as JSON or CSV.
- **Error handling and concurrency:** checked input, bounds-checked buffers,
  cleanup on every path, a mutex per store, and `flock` between processes.

## Building and Running

Requirements: `clang` and `make`. `nc` (netcat) is optional. This was built
and tested on macOS (Apple clang 17, arm64) only; I did not try Linux.

Run every command from the `capstone_project` folder, because the programs
use relative paths such as `data/accounts.dat`.

```
make
```

This builds three programs into `build/` with `-std=gnu11 -Wall -Wextra
-Werror`, so any warning fails the build.

| Program | What it does |
|---|---|
| `build/billing_server` | The billing server. Listens on 127.0.0.1:9090 only. |
| `build/billing_admin` | Local admin tool: `create <name>`, `list`, `rename <id> <new name>`, `delete <id>` |
| `build/billing_client` | Mock usage client: sends a short script of usage events for one account |

### A first run

```
./build/billing_admin create "Fake company"
./build/billing_server
```

The first command creates account 1 and its five meters. The server then
keeps running; press Ctrl+C to stop it cleanly. In a second terminal:

```
./build/billing_client 1
printf 'INVOICE 1 0 9999999999\nQUIT\n' | nc 127.0.0.1 9090
```

The client sends five `USAGE` lines, then `TOTAL` and `QUIT`, and prints
each reply. The second command asks for an invoice covering all usage (from
time 0 to a time far in the future). The server replies `OK <length>` and
then that many bytes of JSON.

### Tests

Each test program prints `PASS` or `FAIL` for every check and uses its own
temporary files, so it does not touch real data.

| Command | What it checks | Checks |
|---|---|---|
| `make test` | Account and meter CRUD, bad input, storage peak, overflow, data surviving a restart | 28 |
| `make test_ledger` | Appending, totals, periods, callbacks, restart, a half-written last line, damaged files | 30 |
| `make test_pricing` | Every rate and tier boundary, half-up rounding, overflow, formatting | 51 |
| `make test_invoice` | Invoice building, CSV and JSON output, escaping | 33 |
| `make test_protocol` | Every command, parsing limits, replies, the line reader | 48 |

## Project Layout

| Path | Contents |
|---|---|
| `include/` | Seven headers: `common.h`, `account.h`, `meter.h`, `ledger.h`, `pricing.h`, `invoice.h`, `protocol.h` |
| `src/` | The implementation of each module, plus `server.c`, `admin.c` and `client.c` |
| `tests/` | The test programs |
| `data/` | Data files created at run time (not committed) |
| `specs.md` | The design spec written before any code |
| `AI_log.md` | Log of every AI consultation |
| `ai_review_1.md`, `ai_review_2.md` | The full AI reviews, saved unedited |
| `Makefile` | Builds everything with `make` |
