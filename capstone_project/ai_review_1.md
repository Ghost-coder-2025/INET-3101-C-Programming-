Architecture review: Cloud Services Billing Engine
Fix these first
Define the billing period and what each usage event means. Today everything is lifetime totals, and STORAGE is ambiguous (see 4.1 and 4.2).
Pick one source of truth for usage, the ledger or the meters (4.3).
Fix the flock/mutex mental model. flock does not exclude threads that share an fd, and “lock before writes” is too narrow (5.1 and 5.2).
Add a consistent multi-resource snapshot to the ledger API, so an invoice can’t mix totals from different moments (3.2).
Complete the error model. IO, lock, overflow and corruption currently have no wire reply and no status code (1.1).
1. Header and API design

1.1 [HIGH] The error model has holes.

status_t has BILL_ERR_IO, NOMEM and LOCK, but the protocol has no reply for them. A disk-full server has nothing to say to the client.
meter_create “fails if one already exists” but there is no BILL_ERR_EXISTS. BILL_ERR_INVALID would conflate it with bad input.
Overflow is folded into BILL_ERR_INVALID (see pricing_cost).
A corrupt log has no code at all.
Fix: add BILL_ERR_EXISTS, BILL_ERR_OVERFLOW and BILL_ERR_CORRUPT. Add ERR internal and ERR busy to the protocol. Write one status_to_proto_error() mapping function. Add status_to_string() for server logs.

1.2 [MEDIUM] proto_error_t mixes parse errors with semantic errors.

protocol_parse can never return UNKNOWN_ACCOUNT, because it doesn’t know which accounts exist. The header comment (“returns the reason the line was rejected”) is misleading.
Error precedence is unspecified. For USAGE 99 BOGUS -5, which error wins?
Fix: state precedence in the spec (for example: framing, then command, then type, then quantity, then account existence). Write the parser as a strict digits-only function. strtoll alone accepts +5,  5, 0x10 and 5abc (with the right end-pointer check it rejects the last, but you must do that check).
Decide whether quantity 0 is valid, and whether a non-numeric account id is bad_command or unknown_account.

1.3 [MEDIUM] There is no line-framing API, and line_too_long can’t work as designed.

TCP is a byte stream. A recv() can return half a line or three lines. Nothing in the headers handles buffering per connection.
If the reader uses a 256-byte buffer, it never hands protocol_parse a line of 256 or more, so the line_too_long check in the parser is unreachable. The reader has to detect the overflow.
After an overlong line, the reader must discard bytes up to the next \n. Otherwise the tail is parsed as a new command.
Fix: add a conn_reader_t with read_line() returning OK, TOO_LONG or EOF. Specify whether 256 includes the \n. Decide how to treat \r\n (telnet and nc send it) and embedded NUL bytes. The simplest rule is to reject both as bad_command.

1.4 [MEDIUM] File-backed modules take a const char *path on every call, while the ledger is a handle.

Per-call paths mean opening, locking and closing the file on every operation.
They also invite mixing up accounts_path and meters_path, since both are const char *.
Fix: use opaque handles (account_store_t, meter_store_t) opened once. This matches ledger_t, and it gives you one place to hold the fd and the lock policy.

1.5 [MEDIUM] Functions the implementation will clearly need are missing.

Account iteration or listing. The admin program needs it, and so does “invoice every account”.
A multi-resource total (see 3.2).
A reconcile function that rebuilds meters from the ledger (see 4.3).
A defined trigger for invoice generation. There is no INVOICE command and no admin command. See 5.5, because where this runs matters architecturally.

1.6 [LOW] The headers don’t state contracts.

Document thread-safety per module.
Say whether account_create rejects or truncates a name over 63 chars. Rejecting with INVALID is better.
Document NULL handling and which out-params are written on failure.
2. Struct layouts and on-disk safety
Struct	sizeof	Notes
account_t	80	8+4+1+3+64, no padding
meter_t	16	8+4+1+1+2, no padding
ledger_entry_t	32	29 bytes of fields plus 3 bytes of trailing padding
invoice_t	168	4 bytes of padding before lines

Your field ordering for the two disk records is good. The remaining problems are about how they get used.

2.1 [MEDIUM] Uninitialized bytes will reach the disk.

reserved[] and the unused tail of name[64] will contain stack garbage unless you memset the struct first.
That makes files nondeterministic and can leak memory contents.
Fix: always memset(&rec, 0, sizeof rec) before filling. Add _Static_assert(sizeof(account_t) == 80, ...) and the same for meter_t.

2.2 [MEDIUM] The binary files have no header, version or validation on load.

Records are native-endian and unversioned. Any later struct change silently corrupts old files.
Fix: put a small header at the start of each .dat file (magic, version, record size). After every fread, validate that deleted is 0 or 1, that type < RES_COUNT, that name is NUL-terminated, and that the stored id matches the slot.

2.3 [MEDIUM] Record addressing isn’t defined, and it affects correctness.

Fix for accounts: make id = slot_index + 1. Reads become a single seek, and you must never compact the file.
Fix for meters: use slot (id-1)*RES_COUNT + type, and create all five meters when the account is created. This enforces “one meter per account per type” structurally. It removes the create-race and the O(n) scan, and meter_create mostly disappears.

2.4 [LOW] ledger_entry_t and invoice_t have padding, which is fine only because they never go through fwrite. Keep it that way. The text ledger is the right call.

3. ADT modularity

3.1 [MEDIUM] ledger_for_each is a deadlock and stall hazard. The callback will run under the ledger mutex.

If it calls back into any ledger function (say ledger_total), you self-deadlock on a non-recursive mutex.
A slow callback, such as one writing CSV to disk, blocks every client thread.
Fix: snapshot the account’s entries into a private array under the lock, release, then iterate. Or at minimum document “callback must not call the ledger and must be fast”.

3.2 [MEDIUM] invoice_build will need five separate ledger_total calls, each taking the lock separately.

A USAGE can land between calls, so the invoice mixes totals from different instants and can’t be reproduced.
Fix: add ledger_totals(ledger, account_id, int64_t out[RES_COUNT], uint64_t *as_seq), which takes one lock and returns an as-of sequence number. Record as_seq on the invoice.

3.3 [MEDIUM] ledger_total is probably an O(n) scan under the global mutex. Every TOTAL command and every invoice scans all entries while blocking all clients.

Fix: keep running totals per (account, type) in memory, updated on append and rebuilt at startup. TOTAL becomes O(1). Keep per-account entry lists for for_each.

3.4 [MEDIUM] invoice is coupled to the account file format.

invoice_build takes accounts_path and parses account records itself. invoice.h doesn’t even include account.h, so the dependency is hidden.
Fix: have it take const char *account_name or const account_t *. Then invoicing is a pure function of ledger totals plus a name, and it is easy to unit-test.

3.5 [LOW] The ledger’s thread-safety is left implicit. Make the ledger own its mutex internally, so no caller can forget to lock, and state that in the header. Don’t expose a lock/unlock pair.

4. Spec and billing design flaws

4.1 [HIGH] There is no billing period.

Totals are lifetime, so “first 1,000 MB free” is a one-time lifetime allowance, not a monthly one.
Invoices have no period, no invoice id, and nothing to prevent billing the same usage twice.
Fix: add period_start, period_end and invoice_id to invoice_t. Add a time-ranged total (ledger_totals_range, or a from/to filter on for_each). Apply tiers and free allowances per period.

4.2 [HIGH] STORAGE semantics are broken.

The ledger sums all events. If clients report the current MB stored (a gauge), the sum counts the same data every report.
If clients report deltas, quantity is non-negative (bad_quantity), so deleting data can never reduce the bill.
Real storage is billed as GB-months, which needs a time dimension.
Fix: choose one. Either storage is a gauge and the invoice uses the latest value, or the maximum or time-weighted average within the period, or you make it a delta type that allows negative values. Then state explicitly that every other type is a delta.

4.3 [HIGH] Ledger and meters are two sources of truth, written non-atomically.

Nothing says which one TOTAL and invoices read. invoice_build uses the ledger, so what are the meters for?
A crash between ledger_append and meter_add_usage leaves them permanently different.
Fix: the ledger is authoritative and meters are a derived cache. The write order is append (and fsync) to the ledger, then update the meter. On startup, rebuild meters from the ledger.

4.4 [HIGH] The ledger isn’t crash-safe.

A crash or disk-full mid-append leaves a partial last line. The loader needs a policy: detect it, truncate or ignore it, and validate that sequence numbers are contiguous.
If the file write fails after the in-memory update, memory and disk diverge and a sequence number is burned.
Fix: build the entry, write the whole line with a single write() on an O_APPEND fd, and update memory and the sequence number only after success. On a failed write, ftruncate back to the previous size, or put the ledger into a failed state.
Reply OK only after fflush plus fsync. Otherwise OK doesn’t mean the event is recorded.

4.5 [MEDIUM] The arithmetic can overflow, and a client can trigger it.

seconds * 40000 overflows int64 at about 2.3e14 total seconds. MAX_QUANTITY is 1e9 per event, so roughly 230,000 maximum-size events from an unauthenticated client get you there. RAM overflows at about 1.8e15.
The running total and the sum of invoice lines can overflow too.
Fix: use __int128 for intermediates, or split the computation as (s/3600)*rate + (s%3600)*rate/3600. Use __builtin_add_overflow in ledger totals and invoice sums, and return BILL_ERR_OVERFLOW.

4.6 [MEDIUM] The rounding rules are unspecified.

The spec says each line “rounds to the nearest micro-dollar”, but seconds*40000/3600 in C truncates.
Lines are rounded in micro-dollars but the total is “shown” in cents. The displayed lines then don’t sum to the displayed total, and it’s unclear which number is the amount charged.
Fix: specify half-up (for example (x*rate + 1800)/3600). Define the charged amount as the cent-rounded total, and say so on the invoice.

4.7 [MEDIUM] Deleted accounts can’t be invoiced. account_read treats deleted accounts as not found, so invoice_build returns NOT_FOUND. But a customer who deleted their account still owes for last month.

Fix: add an account_read_any that includes deleted records. Define “deleted” as blocking new usage only.

4.8 [MEDIUM] There is no idempotency. If the connection drops after the server records a USAGE but before the client sees OK, the client’s retry double-bills.

Fix: an optional client-supplied event id with dedupe, or at minimum document that delivery is at-least-once and that retries may double-count. Also specify what USAGE returns after OK (the new total? the seq?).

4.9 [LOW] Smaller spec issues.

Hardcoded rates mean changing prices re-prices history. Snapshot a rate-card version on each invoice.
There is no authentication, so any client can post usage for any account. Fine for a demo, but say so.
CSV and JSON output of account_name needs escaping. Leading =, +, - or @ is a spreadsheet-formula injection risk, and " and control characters break JSON. Emit money as integer micro-dollars or a string, never a double.
The append-only ledger has no correction mechanism. Real systems add adjustment entries.
An unbounded log means unbounded startup time and memory. Plan for rotation or checkpoints later.
5. Concurrency

5.1 [HIGH] flock() won’t protect you the way the spec implies.

flock locks attach to the open file description, not the thread. If threads share one FILE* or fd, flock(LOCK_EX) from a second thread succeeds immediately, because it is the same description. Between threads, only the mutex protects you. flock only coordinates separate processes (the server and the admin program) or separate open() calls.
Fix: state the model explicitly. The mutex serializes threads. flock serializes processes. Don’t rely on flock for intra-process exclusion.

5.2 [HIGH] “flock() is taken before any file write” is too narrow.

Read-modify-write sequences (meter_add_usage, and account_create computing the next id) must hold LOCK_EX across the read and the write. Otherwise you get lost updates.
Reads need LOCK_SH. Otherwise you can read a half-updated record.
With stdio, buffering defeats the lock. You must fflush before LOCK_UN, and a reader’s FILE* can serve stale buffered bytes after another process writes.
Shared-FILE* fseek+fread/fwrite pairs also race between threads.
Fix: use raw fds with pread/pwrite for the record files, which have no shared offset. Then flock plus a mutex is well-defined.

5.3 [MEDIUM] Lock ordering and blocking locks can stall or deadlock.

A blocking flock while holding the global ledger mutex means one stuck admin process freezes every client thread.
Cross-process deadlock is possible if different code paths take accounts, meters and ledger locks in different orders.
Fix: adopt a rule that no thread holds two locks at once. For USAGE, that means: account_read (SH, release), then ledger_append (mutex, release), then meter_add_usage (EX, release). If you ever must nest, document one order (accounts, then meters, then ledger). Use LOCK_NB with a short retry and timeout, and map BILL_ERR_LOCK to ERR busy.

5.4 [MEDIUM] There are check-then-act races with the admin process.

The server validates the account, then appends. The admin can delete the account in between. The server’s mutex can’t prevent this, since the admin is another process.
Fix: don’t try to prevent it. Define the semantics: usage that passed validation is valid and billable (see 4.7).

5.5 [MEDIUM] Process ownership of the ledger is undefined.

The ledger lives in the server’s memory, but invoices are generated somewhere. If the admin program builds its own ledger_t from ledger.log while the server appends, it can read a partial last line.
Two server instances would each assign the same sequence numbers.
Fix: take a non-blocking LOCK_EX on a separate data/server.lock for the server’s lifetime, as a single-instance guard. Don’t lock ledger.log itself that way, because readers still need it. Then either (a) the admin reads ledger.log with LOCK_SH and tolerates a truncated tail, or (b) invoices are generated inside the server via an admin command. Option (b) removes most cross-process complexity.

5.6 [MEDIUM] Thread lifecycle problems are baked into one-thread-per-connection.

With no connection limit, threads are unbounded.
With no SO_RCVTIMEO, a client that never sends \n holds a thread forever.
Unjoined, non-detached threads leak.
On shutdown, ledger_destroy can run while threads are still inside the ledger (use-after-free). You need a shutdown flag plus a join or a thread counter.
Writing to a closed socket raises SIGPIPE, so ignore it and use MSG_NOSIGNAL.
Pass the accepted fd to the thread via a heap-allocated copy, not a pointer to a loop-local variable.
Fix: add a max-connection cap, a receive timeout, pthread_detach with an active-thread counter, and signal-driven graceful shutdown.

5.7 [LOW] Throughput and portability notes.

fsync under the global mutex serializes all writers. That is acceptable for a demo, but know it is the bottleneck.
Keep data/ on local disk. flock behavior on network filesystems is unreliable.