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

Each invoice line rounds to the nearest micro-dollar. The invoice total is
shown in dollars and cents.

## 3. Records
**Account** (accounts.dat): id, name (max 63 chars), created_at, deleted flag.

**Meter** (meters.dat): account_id, type, total_quantity, deleted flag.
One meter per account per resource type.

**Ledger entry** (ledger.log): seq, account_id, type, quantity, timestamp.

## 4. TCP protocol
Text, one command per line ending in \n. One reply line per command.
Port 9090. Maximum line length 256 bytes.

| Command                          | Meaning                              |
|----------------------------------|--------------------------------------|
| USAGE <account_id> <TYPE> <qty>  | Record one usage event               |
| TOTAL <account_id> <TYPE>        | Running total for one resource       |
| QUIT                             | Close the connection                 |

Valid TYPE values: COMPUTE_CPU, COMPUTE_RAM, STORAGE, NET_IN, NET_OUT.

Replies: `OK <value>` on success, or `ERR <reason>` where reason is one of
unknown_account, bad_type, bad_quantity, bad_command, line_too_long.

The server assigns timestamps. Accounts are created and deleted by a local
admin program, not over the network.

## 5. Files on disk (data/ folder)
- accounts.dat and meters.dat: fixed-size binary records, full CRUD with
  fopen/fseek/fread/fwrite. Delete sets the deleted flag.
- ledger.log: text, one line per event, append-only:
  `seq,account_id,type,quantity,timestamp`
  Rebuilt into memory on startup.
- flock() is taken before any file write.

## 6. Concurrency
One pthread per client connection. A mutex protects the in-memory ledger.
