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
- **Results:** tests/test_stores.c runs 28 checks (CRUD for accounts and meters, bad input, storage peak, overflow, data surviving a restart) and all pass. The leaks tool reports 0 leaks (188 allocations, all freed).
- **Still to do:** Run ASan or Valgrind on CSELabs Linux for overflow and use-after-free checks.
- **Code changes:** src/common.c, src/account.c, src/meter.c, tests/test_stores.c, Makefile.

---
