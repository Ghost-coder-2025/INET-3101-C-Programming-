I went through every file you sent. Three things weren’t included, so I couldn’t check them: common.c (resource_from_string, resource_to_string, resource_is_gauge), pricing.c, and the admin program. Several cross-process findings depend on what the admin program does, and I mark those as conditional below.

The code is careful overall. No lock is held during network I/O. close() runs under the clients lock, so shutdown() can never hit a reused fd. The parser is tight. I found no way for a client to crash the server. The real problems are in cross-process ledger handling, blocking flock, durability on macOS, and slot exhaustion.

High

1. Ledger has no cross-process locking, and load_log truncates (conditional on the admin program)
ledger.c: ledger_create / load_log / ledger_append

The stores use flock, but ledger.log has no lock at all. Suppose the admin program opens the ledger with ledger_create (for example, to print an invoice) while the server is running:

The admin’s load_log reads to EOF at size S.
The server appends seq N, fsyncs it, and replies OK N to a client.
The admin calls ftruncate(S), which deletes the committed entry N.
The server’s next append writes seq N+1 where N used to be.
On restart, the sequence numbers aren’t contiguous, so BILL_ERR_CORRUPT is returned and the server refuses to start.

The same corruption happens if the admin ever calls ledger_append. Each process has its own next_seq, so both will write the same sequence number.

Fix: Make the server the only writer.

Take flock(LOCK_EX | LOCK_NB) on the ledger for the server’s lifetime, or at least around append and truncate.
Have readers take LOCK_SH.
Give the admin a read-only loader that never truncates.

If the admin never touches ledger.log, this drops to “document the assumption.”

Medium

2. A slow client can hold a slot forever and lock out everyone else
protocol.c: next_byte / conn_reader_read_line; server.c: client_thread

SO_RCVTIMEO restarts on every read(), so it only catches clients that send nothing at all. A client that connects and sends one byte every 25 seconds, never sending \n, keeps its slot forever. An over-long line has the same effect, because the discard loop also reads indefinitely. Sixteen such connections and every new client gets ERR busy. The server only listens on loopback, which reduces the exposure but doesn’t remove it.

Fix: Track a per-line deadline in the reader, for example “a full line within 30 s” measured with clock_gettime(CLOCK_MONOTONIC), and return READ_LINE_ERROR when it passes. Optionally add a total connection lifetime or idle cap as well.

3. Blocking flock freezes all clients and hangs shutdown
account.c / meter.c: lock_store

flock is called without LOCK_NB, so it waits forever. The header says BILL_ERR_LOCK means “timed out” and maps to busy, but that can never happen.

Scenario: the admin program holds LOCK_EX on accounts.dat and is paused at a prompt, suspended with Ctrl+Z, or stopped in a debugger.

The next client thread blocks in flock while holding the store mutex.
Every other thread then queues on that mutex.
Ctrl+C on the server hangs as well, because main waits for g_active to reach 0 and that thread never returns.

Fix: Loop on flock(mode | LOCK_NB) with a short sleep and a deadline of about 2 s. Return BILL_ERR_LOCK when the deadline passes. The admin program should also hold locks only for one operation, never across user input.

4. stdio error path in ledger_append can resend a failed line
ledger.c: ledger_append

Scenario: the disk is full.

fprintf succeeds into the FILE buffer, and fflush then fails with ENOSPC.
You ftruncate the file, but the FILE’s buffer may still hold the unwritten line. Apple’s libc keeps unflushed bytes on a write error, though I haven’t verified that for this exact case.
next_seq is unchanged, so the next append buffers another line with the same seq N.
A later successful flush can write seq N twice, which produces BILL_ERR_CORRUPT at the next startup.

Two smaller problems on the same path: the FILE’s error flag stays set, and the return value of ftruncate is ignored.

Fix:

Format the line with snprintf into a local buffer.
Write it with one write() on the O_APPEND fd and check for a short write.
On failure, ftruncate and check the result. If that fails too, mark the ledger as failed and reject further appends.

This takes stdio out of the write path completely.

5. Account records aren’t fsynced, so an account ID can be reused after a crash and inherit someone else’s usage
account.c: account_create, account_delete, account_update

Scenario:

The admin creates account 7, “Acme”. The record is only in the page cache.
The server accepts USAGE for account 7, and the ledger entry is fsynced.
The machine loses power or kernel-panics. A normal process crash is safe here.
accounts.dat comes back with 6 records. The next account created, “Beta”, gets id 7, and Beta’s invoice includes Acme’s usage.

A deleted account can also come back as active.

Fix: Fsync after every account write. On macOS, plain fsync doesn’t flush the drive’s write cache; use fcntl(fd, F_FULLFSYNC) for both the accounts file and the ledger if you want real durability. As a defense in depth, the server could reject ledger entries that are older than the account’s created_at.

6. Account records aren’t validated after reading, so a corrupt name causes an out-of-bounds read
account.c: read_slot → server.c: do_invoice → invoice.c: invoice_build

If accounts.dat has a record whose name has no '\0' (from a torn write, an admin bug, or a hand-edited file), strlen(account_name) in invoice_build reads past the 64-byte array. That’s undefined behavior, and it happens before your >= MAX_NAME_LEN check can catch it.

Fix: In read_slot, check that:

memchr(rec.name, '\0', MAX_NAME_LEN) != NULL
rec.id == id
rec.deleted <= 1

Return BILL_ERR_CORRUPT if any check fails. Also add _Static_assert(sizeof(account_t) == 80, ...) and the equivalent for meter_t.

7. Missing meters for accounts created after startup
server.c: do_usage, do_total (conditional on the admin program)

If the admin creates an account while the server is running and doesn’t also create its 5 meters, then for that account:

every USAGE logs “meter update failed”, and
TOTAL returns ERR unknown_account even though the account exists. BILL_ERR_NOT_FOUND from the meter is mapped to the account error.

This lasts until the server restarts.

Fix: In do_usage, if meter_apply_usage returns NOT_FOUND, call meter_create and retry, as rebuild_meters already does. In do_total, return 0 or internal for a missing meter rather than unknown_account.

Low

8. Linear scans hold the ledger mutex (ledger.c: ledger_totals; server.c: rebuild_meters)

Every INVOICE and TOTAL rebuild scans the whole ledger while holding the mutex, which blocks all USAGE.
Startup costs O(accounts × entries).
Memory grows without limit at about 32 bytes per USAGE.

A client looping on USAGE or INVOICE degrades everyone else. Fsync throttles writes somewhat. For the capstone this is fine to note as a known limitation. A real fix would keep per-account running totals or an index.

9. Account delete races with USAGE (server.c: do_usage)
The account check and ledger_append run under different locks, so usage can be recorded after an admin deletes the account. Because deleted accounts can still be invoiced, this is mostly harmless. Just document it.

10. A retry after a crash can double-bill (protocol design)
If the server crashes after fsync but before sending OK, a client that retries records the usage twice. Delivery is at-least-once. A client-supplied event ID would make retries idempotent. Mentioning this in your write-up is enough.

11. parse_line is looser than it looks (ledger.c)

%llu and %u accept leading whitespace and a - sign; "-1" becomes 4294967295.
Overflow inside sscanf is undefined behavior.

The file is trusted-ish, but parsing with strtoull plus explicit checks, or your own parse_digits, would be stricter.

12. Crash leftovers (ledger.c: load_log)

I’m not certain about this one: a crash can leave a zero-filled tail on some filesystems. In that case fgets returns a line where len == 0, which is BILL_ERR_CORRUPT, so the server won’t start instead of discarding the tail. Treating a NUL-containing final segment like a half-written line would fix it.
After ftruncate, call fseeko(fp, 0, SEEK_END) so the stdio offset is in sync. O_APPEND already protects writes, so this is tidiness.

13. Torn account records on power loss (account.c)
An 80-byte record doesn’t divide 4096, so some records cross page boundaries and can be half-updated, for example a mixed old and new name. Meters are 16 bytes, so they never cross a boundary, and they get rebuilt anyway. To fix, pad account_t to 128 bytes, or add a checksum and validate it under #6.

14. g_stop is read from other threads (server.c)
volatile sig_atomic_t is only guaranteed for the signal handler versus the thread it interrupts. Reading it from client threads is technically a data race in C11. Use _Atomic sig_atomic_t or atomic_int.

15. Return values ignored in server setup (server.c: main)

If setsockopt(SO_RCVTIMEO) fails, there is no idle timeout at all.
If accept fails with EMFILE or ENFILE, poll keeps reporting readiness and the loop spins at 100% CPU. That’s unlikely with 16 slots.

Log setsockopt failures and close the client. On accept errors other than EINTR or ECONNABORTED, sleep briefly.

16. Output escaping gaps (invoice.c: csv_write_name, json_write_name)

CSV formula neutralizing misses a leading tab (\t) and carriage return (\r), which are also on OWASP’s list.
JSON doesn’t escape 0x7f and passes invalid UTF-8 through, which strict parsers reject.

17. Misleading error code (protocol.c: protocol_error_from_status)
An overflow during INVOICE is reported as bad_quantity. It’s cosmetic, but internal would be clearer.

18. Meter slot not cross-checked (meter.c: read_slot)
It doesn’t verify that rec.account_id and rec.type match the slot, which would catch corruption. Separately, meter_create with an id near UINT32_MAX makes a sparse file of about 340 GB. Only the admin program could trigger that.

Checked and found correct

Races (threads):

g_client_fd and g_active are always accessed under g_clients_lock.
Each store and the ledger use their own mutex consistently.
ledger_for_each copies entries before calling back.
Each connection has its own reader, and there are no shared static buffers in the code shown.
Concurrent USAGE on the same meter is safe: addition and max both give the same result in any order.

Deadlock:

The server never holds two store or ledger locks at the same time. rebuild_meters collects ids first, and do_usage does account, then ledger, then meter, one at a time.
Within each store the order is always mutex, then flock.
No lock is held during send.
The server can’t be part of a lock cycle with the admin, whatever order the admin uses. The only risk is the indefinite wait in #3.

Buffers and integers:

conn_reader_read_line bounds: n ≤ 255 against buflen ≥ 256.
split_words correctly rejects empty words, leading or trailing spaces, and too many words.
The overflow test in parse_digits (v > (max-d)/10) is exact.
Overflow checks in ledger_totals, meter_apply_usage and invoice_build are correct.
Struct layouts have no hidden padding: 80 and 16 bytes.
Every format string is a literal, so there are no format-string issues.
The name checks (strnlen, memcpy after memset) are correct.
In client.c, strtoul plus the isdigit check is correct.

Memory and fds:

Every early return in ledger_create, account_store_open and meter_store_open frees what it allocated.
open_memstream’s buffer is freed on both paths.
When pthread_create fails, the fd is closed and the slot is released.
A rejected client’s fd is closed.
Shutdown waits for all threads before destroying stores, so there’s no use-after-free at exit.
free and close are safe with NULL or a missing reader.

Persistence:

The ledger is append-then-fsync before OK, and the in-memory state changes only after a successful write.
A half-written final line is detected and trimmed.
A sequence-number gap is treated as corruption.
Meters are correctly treated as a rebuildable cache.
Unbuffered stdio plus fseeko before every access avoids stale reads and the read/write switching rule.

Hostile clients:

NUL bytes, \r and control characters are rejected.
Zero or negative quantities and bad ranges are rejected.
Over-long lines are handled cleanly.
SIGPIPE is ignored, and SO_SNDTIMEO drops clients that stop reading.
The 17th connection gets busy rather than a thread.
Binding only to loopback is the right choice given there’s no authentication.

Once you send pricing.c, common.c and the admin program, I can confirm or rule out #1 and #7 and check the rounding and overflow logic in pricing_cost.