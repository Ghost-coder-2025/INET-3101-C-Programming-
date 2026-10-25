#ifndef INVOICE_H
#define INVOICE_H

#include <stdio.h>
#include <stdint.h>
#include "common.h"
#include "ledger.h"

/* One line of an invoice: one resource type. */
typedef struct {
    int64_t quantity;                /* usage in the period (peak for a gauge resource) */
    money_t cost;                    /* price of that usage, in micro-dollars */
} invoice_line_t;

/* A complete invoice for one account over one period. */
typedef struct {
    int64_t        generated_at;     /* when the invoice was built */
    int64_t        period_start;     /* usage counted from here (included) */
    int64_t        period_end;       /* ... up to here (not included) */
    uint64_t       as_seq;           /* last ledger entry the invoice could see */
    money_t        total;            /* sum of all line costs, in micro-dollars */
    uint32_t       account_id;
    char           account_name[MAX_NAME_LEN];
    invoice_line_t lines[RES_COUNT]; /* one line per resource, indexed by resource_t */
} invoice_t;

/* Build an invoice for one account over [from, to). The caller looks up the
   account first (use account_read_any, so a deleted account can still be
   invoiced) and passes its name in. Gets all five totals from one
   ledger_totals call, prices each, and sums them. Free allowances and tiers
   apply once per invoice period. Returns BILL_ERR_INVALID if from >= to or
   the name is too long, and BILL_ERR_OVERFLOW if any sum would overflow. */
status_t invoice_build(ledger_t *ledger, uint32_t account_id,
                       const char *account_name, int64_t from, int64_t to,
                       invoice_t *out);

/* Write the invoice as CSV to an already-open stream (a file or stdout).
   Money is written as whole micro-dollars, never as a decimal number.
   Account names are quoted, and a leading = + - or @ is neutralized so
   spreadsheets don't treat it as a formula. Returns BILL_ERR_IO if a
   write fails. */
status_t invoice_write_csv(const invoice_t *inv, FILE *out);

/* Write the invoice as JSON to an already-open stream. Money is written as
   whole micro-dollars; the account name is escaped (quotes, backslashes,
   control characters). The total is also given as a rounded-to-the-cent
   display string. Returns BILL_ERR_IO if a write fails. */
status_t invoice_write_json(const invoice_t *inv, FILE *out);

#endif /* INVOICE_H */
