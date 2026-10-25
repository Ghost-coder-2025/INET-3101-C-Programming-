# Cloud Services Billing Engine: Spec

## 1. Overview
Mock cloud clients report resource usage to a billing server over TCP.
The server records each event in an append-only ledger, prices usage,
and generates invoices as CSV and JSON.

## 2. Pricing
All money is stored as 64-bit integer micro-dollars (1 USD = 1,000,000).
Rates are modeled on public AWS-style pricing; tier thresholds are scaled
down so a demo can show them.

| Resource    | Client reports | Rate (micro-dollars)                                        |
|-------------|----------------|-------------------------------------------------------------|
| COMPUTE_CPU | vCPU-seconds   | 40,000 per vCPU-hour: cost = seconds * 40000 / 3600         |
| COMPUTE_RAM | GB-seconds     | 5,000 per GB-hour: cost = seconds * 5000 / 3600             |
| STORAGE     | MB stored      | Floored to whole GB (MB / 1024). First 100 GB at 23,000/GB, above that 20,000/GB |
| NET_IN      | MB             | 0 (free)                                                    |
| NET_OUT     | MB             | First 1,000 MB free, next 10,000 MB at 90/MB, above that 85/MB |

Rounding: each line rounds half-up to the nearest micro-dollar. The amount
a customer is charged is the invoice total rounded half-up to a whole cent.
Arithmetic is checked for overflow and reports an error instead of wrapping.

## 3. Billing periods
An invoice covers a period [from, to): usage at time "from" is included,
usage at time "to" is not. Free allowances and tier thresholds apply once
per invoice period.

Resource types are two kinds:
- STORAGE is a gauge. Clients report the current MB stored, and the invoice
  bills the peak value reported during the period.
- Every other resource is a running amount. The invoice bills the sum of
  all events in the period.

An invoice records its period and the sequence number of the last ledger
entry it could see, so it can be reproduced.

## 4. Records
**Account** (accounts.dat): id, name (max 63 chars), created_at, deleted flag.
Account N lives in slot N-1 of the file; records are never moved.

**Meter** (meters.dat): account_id, type, total_quantity, deleted flag.
The meter for (account, type) lives in slot (id-1)*5 + type. All five meters
are created when an account is created.

**Ledger entry** (ledger.log): seq, account_id, type, quantity, timestamp.

The ledger is the source of truth. A meter is only a cached total used to
answer TOTAL quickly, and meters are rebuilt from the ledger at startup.

Records are zeroed before they are written, and validated after they are
read (flags are 0 or 1, type is valid, name ends with a NUL byte).

## 5. TCP protocol
Text, one command per line ending in \n. One reply line per command.
Port 9090. A line may have at most 255 characters, not counting the \n.

| Command                          | Meaning                              |
|----------------------------------|--------------------------------------|
| USAGE <account_id> <TYPE> <qty>  | Record one usage event               |
| TOTAL <account_id> <TYPE>        | Running total for one resource       |
| QUIT                             | Close the connection                 |

Valid TYPE values: COMPUTE_CPU, COMPUTE_RAM, STORAGE, NET_IN, NET_OUT.

Replies: `OK <value>` on success (USAGE returns the sequence number, TOTAL
returns the total), or `ERR <reason>`: unknown_account, bad_type,
bad_quantity, bad_command, line_too_long, busy, internal.

Parsing rules, checked in this order:
1. The command word must be USAGE, TOTAL or QUIT (bad_command).
2. Words are separated by exactly one space, with the right number of
   words; a \r or other control character is rejected (bad_command).
3. The account id is digits only, 1 to 4294967295 (bad_command).
4. The type matches one of the five names exactly (bad_type).
5. For USAGE, the quantity is digits only, 1 to 1,000,000,000 (bad_quantity).
Whether the account exists is checked by the server after parsing.

TCP is a byte stream, so a per-connection reader collects bytes into whole
lines. A line that is too long is rejected and the rest of it is discarded.
A line containing a NUL byte is rejected.

The server assigns timestamps. Accounts are created and deleted by a local
admin program, not over the network. A deleted account cannot receive new
usage, but can still be invoiced.

## 6. Files on disk (data/ folder)
- accounts.dat and meters.dat: fixed-size binary records, full CRUD with
  fopen/fseek/fread/fwrite. Delete sets the deleted flag.
- ledger.log: text, one line per event, append-only:
  `seq,account_id,type,quantity,timestamp`
  Loaded into memory on startup. A half-written last line (from a crash) is
  ignored; a bad line anywhere else is reported as corruption.
- A ledger append is only reported as successful after it is written and
  flushed to disk. On failure, nothing changes and no sequence number is used.

## 7. Concurrency
- One pthread per client connection, with a connection limit, a receive
  timeout, and no unbounded threads.
- Each store (accounts, meters) and the ledger owns its own mutex, which
  serializes threads. flock coordinates separate processes (the server and
  the admin program). flock is not relied on between threads.
- Read-modify-write sequences hold the lock across both the read and the
  write.
- A thread never holds two locks at once. For USAGE: look up the account,
  release; append to the ledger, release; update the meter, release.
- SIGPIPE is ignored, and the server shuts down cleanly (no thread is inside
  the ledger when it is destroyed).

## 8. Known limits (for the README)
No authentication; retries after a dropped connection can double-count
usage; rates are fixed in code, so changing a price would re-price history;
no ledger rotation or adjustment entries; storage is billed at peak, not as
a time-weighted average.

## 9. Open items
- How an invoice is triggered (a server command or the admin program).
  To be decided in Phase 7.
