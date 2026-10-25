# AI Log: Cloud Services Billing Engine

Running record of every AI consultation. One entry per session.

## Entry template

### Entry N: <short title> (<date>)
- **Phase:** which project phase this was for
- **Tool:** ChatGPT / Claude
- **Prompt:** the exact prompt used
- **AI response (summary):** what it suggested
- **Accepted:** suggestions I used
- **Rejected / wrong:** suggestions I declined, or things the AI got wrong or missed
- **Code changes:** what I changed as a result

---

## Entries

(none yet)
### Entry 1: Header and spec architecture review (2026-10-24)
- **Phase:** 3, architecture review of spec and headers before any implementation
- **Tool:** separate fresh AI chat with no prior project context
- **Prompt:**
  > You are a senior C systems engineer doing an architecture review. I'm a student building a "Cloud Services Billing Engine" in C for a class capstone (multi-file C, file-based CRUD persistence, TCP client/server, pthreads and flock for concurrency). I have written the design spec and the header files ONLY. No implementation (.c files) exists yet, and I want to find design problems now, before I write any logic.
  > Please review the spec and headers for: (1) header and API design, (2) struct layouts and whether records are safe to write with fwrite/fread, (3) ADT modularity of the ledger, (4) design flaws in the spec itself (consistency, billing correctness, integer overflow), (5) concurrency design given one pthread per client, a mutex around the ledger, and flock on the files.
  > For each issue, give a severity (high/medium/low), a short explanation, and a concrete suggested fix. Be critical and specific. Do not write the implementation.
- **AI response (summary):** About 35 findings across the five areas. Saved in full in `ai_review_1.md`. Top issues: no billing period, ambiguous STORAGE semantics, ledger and meters as two sources of truth, flock/mutex misunderstanding, incomplete error model.
- **Accepted (header changes):** complete the error model; add a one-lock multi-resource totals function to the ledger; make invoicing take an account name instead of an accounts file path; add a billing period to invoices; make the ledger authoritative and meters a cache; add a way to read deleted accounts for invoicing; add a connection line reader; use open store handles instead of a path on every call.
- **Accepted (handle in implementation or spec):** memset records before writing, validate on load, id = slot + 1, overflow checks, half-up rounding, never hold two locks at once, connection cap and timeouts, SIGPIPE handling, clean shutdown.
- **Accepted (design decisions I made):** an invoice covers a from/to period; STORAGE is a gauge billed at the peak MB in the period; all other resources are running amounts.
- **Rejected / wrong:**
  - It recommended raw file descriptors with pread/pwrite for the record files. The assignment requires fopen/fread/fwrite or gdbm, so I am keeping stdio with a mutex per store, fflush before unlock, and flock on fileno(fp).
  - It said strtoll accepts "0x10". With base 10 it does not (it stops at the x). The advice to write a strict digits-only parser was still correct.
  - It suggested removing meter_create, which would weaken the visible Meter CRUD. I am keeping it.
  - It did not weigh scope. Authentication, idempotency, rate-card versioning, log rotation and adjustment entries are too large for this project, so they go in the README Pros and Cons as known limits.
- **What it found that I had not told it:** the billing period gap and the STORAGE ambiguity, both of which I had noticed myself.
- **Code changes:** Rewrote all seven headers. common.h: new error codes, resource_is_gauge, status_to_string. account.h and meter.h: open stores instead of file paths, account_read_any, account_for_each, meter_apply_usage, meter_set_total. ledger.h: ledger_totals and ledger_current_totals with billing periods, ledger_for_each now copies entries before calling the callback. invoice.h: period, as_seq, account name passed in. pricing.h: overflow error, half-up rounding, pricing_round_to_cents. protocol.h: conn_reader_t line reader, busy and internal errors, parsing rules. Rewrote specs.md to match. All headers compile clean with the strict flags.

---

### Entry 2: Implementing the account and meter stores (2026-10-24)
- **Phase:** 4, account.c, meter.c, and a test program
- **Tool:** Claude, used for implementation help
- **Prompt:** Asked for account.c and meter.c matching the refactored headers, then asked for a simpler account.c when the first one was too hard to follow.
- **AI response (summary):** The first account.c included the review's extras: a half-second flock retry loop, validation of every record on read, and cleanup of half-written records after a failed create.
- **Accepted:** Fixed-size records addressed by slot number, a mutex plus flock on every operation, memset before writing a record, input checks on names and ids, deleted flag instead of removing records.
- **Rejected / wrong:**
  - The first account.c was too complex for me to understand and explain, so I had it rewritten without the lock retry loop, per-record validation, and failed-create cleanup. These go in the README Pros and Cons as known limits. I kept the locking and input checks because the rubric requires them.
  - AddressSanitizer was suggested for the memory audit, but the ASan build hung at startup on my Mac (macOS 26.5, Apple clang 17, arm64) before any test output. The same code passed in a normal build. I did not find the cause, so I used Apple's leaks tool instead.
- **Results:** tests/test_stores.c runs 28 checks (CRUD for accounts and meters, bad input, storage peak, overflow, data surviving a restart) and all pass. The leaks tool reports 0 leaks (188 blocks still in use at exit, none of them leaked).
- **Still to do:** Run ASan or Valgrind on CSELabs Linux for overflow and use-after-free checks.
- **Code changes:** src/common.c, src/account.c, src/meter.c, tests/test_stores.c, Makefile.

---

### Entry 3: Implementing the usage ledger (2026-10-25)
- **Phase:** 5, ledger.c and test_ledger.c
- **Tool:** Claude, used for implementation help
- **Prompt:** Asked for ledger.c matching the refactored ledger.h, built in two smaller parts so I could follow it (part 1: open, load, append, close; part 2: totals and walking through entries), then a test program.
- **AI response (summary):** An in-memory list of entries plus an append-only text log. Append writes and flushes the line to disk first and only then updates memory, so a failed write changes nothing. Loading checks each line, requires entry numbers to count up with no gaps, and ignores a half-written last line. Totals use one lock and report the last entry number (as_seq). The callback in ledger_for_each runs after the lock is released, on a copy of the entries.
- **Accepted:** All of the above. These came from the Entry 1 review (one-lock totals, as_seq, crash-safe append, callback outside the lock, corrupt-file detection) and are now covered by tests.
- **Rejected / wrong:**
  - The AI described the leaks tool output incorrectly. It said "188 allocations, all freed", but the 188 blocks are ones still in use at exit, mostly from the C runtime, and the identical number for two different test programs showed this. The 0 leaks result was right, only the description was wrong. I had it corrected in Entry 2.
  - The ledger's overflow check (a total too big for 64 bits) cannot be tested without billions of entries. The same idea is tested in the meter tests, and I am listing this as untested in the README.
- **Results:** tests/test_ledger.c runs 30 checks (appending, bad input, totals, storage peak, time periods, callback re-entry, restart, half-written last line, garbage line, gap in numbers). All pass. The leaks tool reports 0 leaks.
- **Still to do:** Run ASan or Valgrind on CSELabs Linux for overflow and use-after-free checks, since ASan hangs on my Mac.
- **Code changes:** src/ledger.c, tests/test_ledger.c, Makefile (test_ledger rule).

---

### Entry 4: Implementing the pricing engine (2026-10-25)
- **Phase:** 6, pricing.c and test_pricing.c
- **Tool:** Claude, used for implementation help
- **Prompt:** Asked for pricing.c matching the refactored pricing.h and the rates in specs.md, then a test program using examples I can check by hand.
- **AI response (summary):** Rates are named constants at the top of the file. CPU and RAM use seconds x rate / 3600 with half-up rounding. Storage rounds down to whole GB and then uses two tiers. Outbound network has a free allowance and then two tiers. Every multiplication that could overflow is checked first and returns BILL_ERR_OVERFLOW. The customer is charged the total rounded half-up to a whole cent, and pricing_format_usd uses snprintf so it never writes past the buffer.
- **Accepted:** All of it. It implements the review's findings on overflow (4.5) and rounding (4.6).
- **Rejected / wrong:** Nothing wrong found this time. One caution: the AI wrote both the code and the tests, so they could share the same wrong assumption. To reduce that risk, every expected value in the test is a number I can work out by hand from the rates in specs.md (for example 12,000 MB outbound = 900,000 + 85,000 = 985,000), and the 9-second RAM case is exactly 12.5, so it tests the half-up rule directly.
- **Results:** tests/test_pricing.c runs 51 checks (storage to GB, CPU and RAM, storage tiers, network, bad input, overflow for four resources, rounding to cents, formatting including the most negative number and a too-small buffer). All pass. The leaks tool reports 0 leaks (188 blocks still in use at exit, none of them leaked).
- **Code changes:** src/pricing.c, tests/test_pricing.c, Makefile (test_pricing rule).

---
