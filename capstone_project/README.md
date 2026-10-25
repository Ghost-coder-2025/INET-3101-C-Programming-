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
then that many bytes of JSON. The server also saves a CSV copy as
`data/invoice_1.csv`.

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

## Design & Architecture Details

### How the pieces fit

```
mock clients (billing_client, nc)
        |   TCP, text lines, 127.0.0.1:9090
        v
  billing_server --- one thread per client, at most 16
        |
        |  protocol.c   parses each line and builds the reply
        |  account.c    is the account valid?            data/accounts.dat
        |  ledger.c     records the usage event          data/ledger.log
        |  meter.c      updates the cached total         data/meters.dat
        |  pricing.c    turns usage into micro-dollars
        |  invoice.c    JSON reply, plus a CSV copy      data/invoice_<id>.csv

  billing_admin  (a local program, no network)
        creates, lists, renames and deletes accounts, and creates their meters
```

### Modules

Each module has a header in `include/` that says what it offers, and a `.c`
file in `src/` with the code. Other files only use the functions in the header.

| Module | Job |
|---|---|
| `common` | Shared limits, `money_t`, the `resource_t` list, the `status_t` return codes, and name conversion |
| `account` | The accounts store: create, read, update, delete |
| `meter` | The meters store: create, read, apply usage, set total, delete |
| `ledger` | The usage ledger ADT: append-only history, totals, and walking through entries |
| `pricing` | Usage into micro-dollars, with tiers, rounding and overflow checks |
| `invoice` | Builds an invoice, writes it as CSV or JSON |
| `protocol` | Parses client lines, builds replies, and reads whole lines from a socket |
| `server`, `admin`, `client` | The three programs |

### Key data structures

**Money and resources.** `money_t` is a 64-bit whole number of micro-dollars.
`resource_t` lists the five resources (`RES_COMPUTE_CPU`, `RES_COMPUTE_RAM`,
`RES_STORAGE`, `RES_NET_IN`, `RES_NET_OUT`), and `RES_COUNT` is the number of
them, so arrays can have one slot per resource. Every function returns a
`status_t`: `BILL_OK` or one of `BILL_ERR_INVALID`, `NOT_FOUND`, `EXISTS`,
`IO`, `NOMEM`, `LOCK`, `OVERFLOW` and `CORRUPT`.

**`account_t`** (80 bytes), one customer account:

| Field | Meaning |
|---|---|
| `created_at` | When the account was made |
| `id` | The account number, starting at 1 |
| `deleted` | 0 = active, 1 = deleted |
| `reserved[3]` | Unused, so there is no hidden padding |
| `name[64]` | Up to 63 characters plus the ending `'\0'` |

**`meter_t`** (16 bytes), a cached total of one resource for one account:
`total_quantity`, `account_id`, `type`, `deleted`, and `reserved[2]`.

**`ledger_entry_t`**, one usage event: `seq` (starts at 1), `timestamp` (set
by the ledger), `quantity`, `account_id`, and `type`. It is never written to
disk as raw bytes, because the ledger file is text.

**`invoice_t` and `invoice_line_t`.** An invoice holds the account id and
name, the period, `as_seq` (the last ledger entry it could see), the total,
and one line per resource, indexed by `resource_t`. It has a fixed size, so
it needs no heap memory.

**`command_t`**, a parsed client command: its kind (`USAGE`, `TOTAL`,
`INVOICE` or `QUIT`), the account id, the resource type, the quantity, and
the invoice period.

**Why the record sizes are exact.** The account and meter records are written
to disk as raw bytes. The compiler can insert invisible padding inside a
struct, and that padding would contain garbage. The structs are sized to
exactly 80 and 16 bytes with `reserved` fields, and `src/header_check.c`
uses `_Static_assert` so the build fails if either size ever changes.

### Abstract Data Types

Four types are hidden. The header only says `typedef struct name name;`, and
the real struct is defined inside the `.c` file. Other files can hold a
pointer and call the functions, but they cannot reach inside.

| ADT | What is hidden inside | Operations |
|---|---|---|
| `ledger_t` | The log file, a growing array of entries, the next sequence number, and a mutex | create (loads the log), append, totals, current totals, for each, destroy |
| `account_store_t` | The open accounts file and a mutex | open, close, create, read, read any, update, delete, for each |
| `meter_store_t` | The open meters file and a mutex | open, close, create, read, apply usage, set total, delete |
| `conn_reader_t` | The bytes received so far on one connection | create, read line, destroy |

Opening or creating one gives back a pointer through an output parameter.
Closing or destroying it frees everything, and is safe to call with `NULL`.
Each type owns its own mutex, so callers never have to lock anything.

### Persistence strategy

There are three kinds of file in `data/`.

**`accounts.dat` and `meters.dat`: fixed-size binary records.** Every record
has the same size, so the program can jump straight to any record without
searching.

| File | Where a record lives |
|---|---|
| `accounts.dat` | Account N starts at byte (N - 1) x 80 |
| `meters.dat` | The meter for (account id, type) is in slot (id - 1) x 5 + type, at byte slot x 16 |

- The code uses `fopen`, `fseeko`, `fread` and `fwrite`. The file is
  unbuffered, so a read always sees what is really in the file.
- **Create** adds a record at the end. **Read** seeks to the record.
  **Update** reads it, changes it, and writes it back under one lock.
  **Delete** only sets the `deleted` flag. The record stays, so the slots
  never shift and account numbers always match their positions.
- Every record is zeroed with `memset` before it is filled in, so no garbage
  bytes reach the file.
- Account records are checked when read: the stored id must match the slot,
  the `deleted` flag must be 0 or 1, and the name must have an end. A damaged
  record returns `BILL_ERR_CORRUPT`. Meter records are not checked.
- A normal read treats a deleted account as not found. `account_read_any`
  still finds it, because a deleted account can still owe for earlier usage.

**`ledger.log`: an append-only text file.** One line per usage event:

```
seq,account_id,TYPE,quantity,timestamp
5,1,NET_OUT,2000,1761350412
```

The ledger is the source of truth for usage. Its rules:

- **Appending:** make room in memory first, then write the line, flush it, and
  `fsync` it. Only if all of that worked does the in-memory list and the
  sequence counter change. The server replies `OK` only after this, and a
  failed write changes nothing.
- **Loading at startup:** every line is parsed, and entry numbers must count
  1, 2, 3 with no gaps. A half-written last line (a crash mid-write) has no
  newline, so it is ignored and cut off. Any other bad line returns
  `BILL_ERR_CORRUPT`, and the server refuses to start.
- **Meters are only a cache.** At startup the server rebuilds every active
  account's meters from the ledger, and creates any that are missing. If a
  meter update fails after the ledger append, the client still gets `OK`
  (the event is saved, and an error would make the client retry and
  double-count), and the meter is corrected at the next startup.

**`invoice_<id>.csv`.** Each `INVOICE` command also writes a CSV copy of the
invoice to `data/invoice_<account id>.csv`. It is one file per account, and
the next invoice for that account replaces it.

### Concurrency design

- **Threads:** one detached thread per client, with at most 16 at once. A
  17th client gets `ERR busy` and is disconnected. Idle clients are
  disconnected after 30 seconds, and the send timeout is also 30 seconds.
- **Two kinds of lock, one rule for each.** The mutex in each store keeps
  threads apart. `flock` keeps separate processes apart (the server and the
  admin program). The order is always mutex first, then `flock`. Reads take a
  shared lock, and writes take an exclusive one. A read-modify-write sequence
  holds the lock across both steps.
- **Never two locks at once.** A `USAGE` command looks up the account and
  releases, appends to the ledger and releases, then updates the meter and
  releases. This rules out deadlock between the stores.
- **The ledger copies before calling back.** `ledger_for_each` copies the
  matching entries, releases its lock, and only then calls the callback, so a
  slow callback cannot block other clients.
- **One server per ledger.** `ledger_create` takes an exclusive `flock` on the
  ledger file, so a second server is refused instead of trimming an entry the
  first one already saved.
- **Shutdown.** Ctrl+C or SIGTERM sets a stop flag. The server stops
  accepting, shuts down every client socket, waits until every client thread
  has finished, and only then destroys the ledger and closes the stores.
  `SIGPIPE` is ignored, so a client that disconnects mid-reply cannot kill
  the server.
- **The CSV copy** has its own mutex, which is never held together with any
  other lock.

### Socket networking protocol

The server listens on `127.0.0.1:9090` only. There is no authentication, so
it must not be reachable from other machines. The protocol is plain text, one
command per line ending in `\n`, with at most 255 characters per line.

| Command | Meaning | Reply |
|---|---|---|
| `USAGE <account_id> <TYPE> <quantity>` | Record one usage event | `OK <sequence number>` |
| `TOTAL <account_id> <TYPE>` | The running total for one resource | `OK <total>` |
| `INVOICE <account_id> <from> <to>` | The invoice for the period [from, to) | `OK <length>`, then exactly that many bytes of JSON |
| `QUIT` | Close the connection | `OK bye` |

`TYPE` is one of `COMPUTE_CPU`, `COMPUTE_RAM`, `STORAGE`, `NET_IN`, `NET_OUT`.
`from` and `to` are Unix timestamps. A failure is always one line,
`ERR <reason>`, where the reason is `unknown_account`, `bad_type`,
`bad_quantity`, `bad_command`, `line_too_long`, `busy` or `internal`.

An example session (the `>` lines are sent, the `<` lines are replies):

```
> USAGE 1 COMPUTE_CPU 3600
< OK 2
> USAGE 1 NET_OUT 2000
< OK 3
> TOTAL 1 NET_OUT
< OK 2000
> QUIT
< OK bye
```

**Parsing rules.** The server never trusts the client. A line is checked in
this order: the command word, then the number of words (separated by exactly
one space, with no control characters), then the account id (digits only, 1 to
4294967295), then the type (an exact match), then the quantity (digits only, 1
to 1,000,000,000) or, for `INVOICE`, the period (digits only, and `from` must
be less than `to`). Numbers are read by a digits-only function that checks for
overflow before multiplying, so values like `+5`, `-5` or `5abc` are
rejected. Whether the account exists is checked by the server after parsing.

**Framing.** TCP is a stream of bytes, not lines, so each connection has a
reader that collects bytes into whole lines. A line that is too long, or that
contains a NUL byte, is rejected and the rest of it is thrown away, so the
leftover cannot be mistaken for a new command. The `INVOICE` reply is the only
multi-line one, so the server first sends `OK <length>`, which lets the client
know where the JSON ends. The server builds the JSON in memory before sending,
so the length is always exact.

**Internal errors become client replies** in one place: not found becomes
`unknown_account`, invalid becomes `bad_command`, overflow becomes
`bad_quantity`, a lock timeout becomes `busy`, and everything else becomes
`internal`.

### Error handling and cleanup

- Every function returns a `status_t`, so a failure is always reported to the
  caller.
- Bad input is rejected at the edge: the parser checks every field, names are
  limited to 63 characters, and every buffer is written with its size
  (`snprintf`, bounded reads), so nothing can write past the end.
- Every error path in an open or create function frees what it had already
  allocated. Sockets are closed on every path, and the server waits for all
  its threads to finish before closing the stores.
- Pricing checks for overflow before every multiplication, and the ledger and
  meters check before every addition.

## Pros & Cons of Solution

I did not time the server or measure its memory. The performance and memory
points below come from reading the code, and the checks I did run are listed
in the Testing section of the AI Audit Reflection.

### What works well

- **Exact money.** Every amount is a whole number of micro-dollars, so there
  are no floating point rounding errors, and every multiplication and addition
  that could overflow is checked first.
- **Usage is safe once acknowledged.** The ledger line is written, flushed and
  `fsync`ed before the client gets `OK`. A half-written last line after a
  crash is ignored on restart, and the meters (a cache) are rebuilt from the
  ledger.
- **Clear modules.** Seven headers describe the whole design, four types hide
  their contents behind functions, and a header was written and reviewed
  before any implementation.
- **Simple, checkable concurrency.** Each store owns its mutex, `flock`
  coordinates the two programs, and no thread ever holds two locks at once.
  The server limits clients to 16, drops idle ones, and shuts down by waiting
  for every thread before it frees anything.
- **Fast record access.** Fixed-size records are found by a direct seek, with
  no searching, and `TOTAL` reads one cached meter.
- **Easy to test.** The text protocol works with `nc`. Five test programs run
  190 checks, and the build has zero warnings under `-Wall -Wextra -Werror`.
- **Safe by default.** The server listens on 127.0.0.1 only.

### Performance

- **Every usage event costs one `fsync`, under the ledger mutex.** That makes
  writes one at a time, and the disk is the limit on throughput.
- **`INVOICE` scans the whole ledger while holding the ledger mutex.** It
  blocks `USAGE` until it finishes. Startup is slower still, because
  rebuilding the meters scans the ledger once for every account.
- **One thread per client, at most 16.** A real server would use an event loop
  or a thread pool.

### Memory efficiency

- **The whole ledger lives in memory.** Each entry is 32 bytes, so a million
  events is about 32 MB, and the array doubles when it fills. Nothing is ever
  dropped, so memory grows for as long as the server runs.
- **Deleted accounts and meters stay in their files.** This keeps every
  record at its slot, but the space is never reclaimed.
- **Small and fixed elsewhere.** An invoice is one fixed-size struct, the JSON
  reply is built in a buffer that is freed right after sending, and every
  error path frees what it allocated.

### Known limits

| Limit | What it means |
|---|---|
| No authentication | Anyone on the machine can post usage for any account. The server only listens on loopback because of this. |
| Retries can double-count | If the connection drops after the server saves an event but before the client sees `OK`, a retry records it twice. |
| Storage is billed at its peak | Real providers use a time-weighted average. `TOTAL` for storage is the all-time peak, not the peak in a period. |
| Rates are fixed in the code | Changing a price would change what old usage costs. Invoices do not record a price list version. |
| A slow client can hold a connection | The timeout restarts on every byte, so a client sending one byte every 29 seconds keeps its slot. |
| Waiting for a file lock has no timeout | If the admin tool stalled while holding a lock, the server's clients would wait behind it. |
| Power loss is not fully covered | The account file is not `fsync`ed, macOS needs `F_FULLFSYNC` for a true flush, and an 80-byte account record can cross a disk page boundary. |
| A full disk during a ledger append | In theory the buffered line could be written twice. The audit flagged this and did not verify it, and I did not test it. |
| Meter records are not checked on read | Account records are, and a damaged one returns an error. |
| The ledger parser is looser than the protocol parser | It uses `sscanf`, which accepts a leading space or minus sign. The file is written only by the server. |
| No log rotation and no corrections | The ledger never shrinks, startup time grows with it, and a mistake cannot be fixed with an adjustment entry. |
| Invoices come from the server only | The admin tool does not open the ledger, so `billing_admin` cannot produce an invoice. |

### Future expansion

- Keep running totals per account in the ledger so totals and invoices do not
  scan every entry, and add log rotation with checkpoints.
- Add an event id to `USAGE` so retries are safe, and add authentication.
- Store a price list version with each invoice, and bill storage as a
  time-weighted average.
- Add a time limit per line, and a timeout on file locks.
- Use `F_FULLFSYNC` on macOS, pad account records to 128 bytes so they never
  cross a page, and add adjustment entries for corrections.
- Replace one thread per client with a thread pool or an event loop.

## AI Audit Reflection

I used AI in two ways. **Claude helped me implement** each module in small
steps. **Two independent reviews were run in a fresh chat with no project
context**: one on the headers before any implementation, and one on the
finished code. The independent chat has no memory of what was written, so its
blind spots are different from the implementation help. Every consultation is
recorded in `AI_log.md` (nine entries, with the exact prompts), and the full
text of both reviews is saved unedited in `ai_review_1.md` and
`ai_review_2.md`.

| Entry | Phase | What |
|---|---|---|
| 1 | 3 | Architecture review of the spec and headers (fresh chat) |
| 2 | 4 | Account and meter stores |
| 3 | 5 | The ledger |
| 4 | 6 | Pricing |
| 5 | 7 | Invoices |
| 6 | 8 | Protocol, server and admin program |
| 7 | 9 | Hardening review of `server.c` |
| 8 | 10 | Concurrency and leak audit (fresh chat) |
| 9 | 11 | Making the CSV invoice reachable |

### Review 1: architecture, before any implementation

**Prompt.** I sent the spec and all seven headers and asked a senior C
engineer to review them for header and API design, struct layouts and
whether records are safe to write with `fwrite`, the ledger as an ADT, flaws
in the spec itself (consistency, billing correctness, overflow), and the
concurrency design. For each issue I asked for a severity and a concrete fix.
The full prompt is in `AI_log.md`, Entry 1. I did not tell it about two
problems I had already noticed.

**Result.** About 35 findings. It found both problems I had not told it
about: there was no billing period, and the meaning of storage was ambiguous.

**What improved the architecture, and what I changed:**

| Finding | Change |
|---|---|
| No billing period | Invoices cover [from, to), and free allowances and tiers apply per period |
| Storage was ambiguous | Storage is a gauge billed at its peak in the period |
| The ledger and the meters were two sources of truth | The ledger is authoritative, and meters are a cache rebuilt at startup |
| An invoice could mix totals from different moments | `ledger_totals` returns all five totals under one lock, with `as_seq` |
| The invoice code depended on the accounts file | The account name is passed in |
| A deleted account could not be invoiced | Added `account_read_any` |
| A callback ran while holding the ledger lock | `ledger_for_each` copies, releases, then calls back |
| The error model had gaps | Added `EXISTS`, `OVERFLOW` and `CORRUPT`, and the `busy` and `internal` replies |
| TCP has no lines | Added the per-connection line reader |
| `flock` does not exclude threads sharing a file | A mutex per store keeps threads apart, and `flock` keeps processes apart |

**What I rejected or found wrong:**

- It recommended raw file descriptors with `pread` and `pwrite`. The
  assignment requires `fopen`, `fread` and `fwrite` (or gdbm), so I kept stdio
  with a mutex per store.
- It said `strtoll` accepts `"0x10"`. With base 10 it does not, so that claim
  was wrong, although the advice to write a strict digits-only parser was
  right.
- It suggested removing `meter_create`, which would weaken the visible Meter
  CRUD, so I kept it.
- It did not weigh scope. Authentication, idempotency, price list versioning
  and log rotation are too large for this project, so they are listed under
  Known limits.

### Where the AI's help led to mistakes

- **Dropped validation.** The first `account.c` was too complicated for me to
  explain, so I had it simplified. That removed a record check, and the
  second audit later found its exact consequence (see below).
- **A wrong description of the leaks tool.** The AI said "188 allocations, all
  freed". The 188 blocks are still in use at exit and come from the C
  runtime, and the same number for different programs showed it. The 0 leaks
  result was right, and only the description was wrong. I corrected it in the
  log.
- **AddressSanitizer hangs on my Mac.** The ASan build stopped at startup,
  before any output, on macOS 26.5 with Apple clang 17. The same code passed
  in a normal build. I did not find the cause, so I used Apple's `leaks` tool.
- **The CSV invoice could not be reached.** I built the CSV writer in Phase 7
  and the `INVOICE` command in Phase 8 without noticing that nothing connected
  them. I found it while writing this README. `INVOICE` now also saves
  `data/invoice_<id>.csv`.
- **Tests written by the same AI as the code** can share its wrong
  assumptions. To reduce that, the expected values in the pricing tests are
  numbers I could work out by hand from the rates.
- **An empty `data/` folder is not stored by git**, so a fresh clone would
  have had no `data/` and the server would not start. I added
  `data/.gitkeep`.

### Hardening review of `server.c` (Entry 7)

I pasted `server.c` and asked for a review against six items: connection
limit, receive timeout, `SIGPIPE`, clean shutdown, cleanup on every path, and
one lock at a time. All six were already covered, and the one fix was a send
timeout, so a client that never reads its replies cannot hold a thread.

### Review 2: concurrency and leak audit, on the finished code

**Prompt.** I sent all headers plus `server.c`, `client.c`, `protocol.c`,
`ledger.c`, `account.c`, `meter.c` and `invoice.c`, and asked for an audit of
races, deadlocks, buffer and integer problems, leaks and descriptor leaks on
every error path, file persistence problems, and anything a hostile client
could do. I asked for the file, function, severity, a concrete trigger and a
fix, and for the AI to say when it was unsure. I left out `pricing.c`,
`common.c` and `admin.c`, so those files have not had an AI audit. The full
prompt is in `AI_log.md`, Entry 8.

**Result.** About 18 findings, and a long list of things it checked and found
correct. It found no way for a client to crash the server.

**Bugs found and fixed:**

| Finding | The bug | Fix |
|---|---|---|
| Account records not validated | A damaged record with no `'\0'` in its name would make the invoice code read past the 64-byte name. This was the check I had dropped in the simplification. | `account.c` now checks the id, the `deleted` flag, and that the name has an end, and returns `BILL_ERR_CORRUPT` |
| No lock on the ledger file | A second process opening the ledger could trim an entry the first had already acknowledged | `ledger_create` takes an exclusive `flock` first, so a second server is refused |
| `g_stop` read by other threads | A flag meant for a signal handler was read by client threads, which is technically a data race | Changed to `_Atomic sig_atomic_t` |

I checked the two findings that depended on `admin.c` with `grep`: the admin
tool creates each account's five meters and never opens the ledger, so
neither could happen through it.

**Memory leaks.** The audit found no leaks, double frees or use-after-free,
and it confirmed that every error path in the open and create functions frees
what it allocated and that shutdown waits for every thread. This rests on code
reading plus the `leaks` tool on the test programs. I did not run the leaks
tool on the running server.

**Known limits it found, with no fix:** a slow client holding a slot, waiting
for a file lock with no timeout, power loss durability, a possible duplicate
entry number after a full disk, the cost of scanning the whole ledger, and
retries that can double-count. They are listed under Known limits above.

**What it missed or got wrong:**

- It marked two findings as unverified itself (a full disk during an append,
  and a zero-filled ledger tail after a crash), and I did not test either.
- It did not name the startup ordering where the ledger is opened before the
  port is bound. It described the same damage through the admin program, and
  the lock fix covers both.
- It re-found three limits I already knew about (the slow client, retries, and
  the full ledger scans) without being told, which gives me some confidence in
  the review.

### Testing, and what was not tested

- **Automated:** 190 checks across five test programs (28, 30, 51, 33 and 48).
- **By hand:** `nc` sessions for every command and error reply, the mock
  client against the server, the CSV copy checked line by line against the
  JSON and the rates, a second server refused by the ledger lock, and Ctrl+C
  shutdown.
- **Not tested:** the overflow path in the ledger totals (it would take
  billions of entries), the corrupt-record check, the ledger lock and
  `save_csv_copy` have no automated test, and `save_csv_copy` was added after
  the second audit, so the audit has not seen it. I did not run AddressSanitizer
  or Valgrind, and the server was not tested under load with many clients.

### What I would take from this

Reviewing the headers before writing any logic caught the largest design
problems (the billing period, storage, and the two sources of truth) while
they were cheap to change. The independent chat found things the
implementation help did not. The cost was that AI answers need checking: it
gave wrong claims (`pread`, `strtoll`, the leaks output), its code and tests
can share assumptions, and simplifying code for my own understanding removed a
safety check that mattered. The audit caught that one.
