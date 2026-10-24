#ifndef INVOICE_H
#define INVOICE_H

#include <stdio.h>
#include <stdint.h>
#include "common.h"
#include "ledger.h"

/* One line of an invoice: one resource type. */
typedef struct {
    int64_t quantity;                /* total used, in the resource's unit */
    money_t cost;                    /* price of that usage, in micro-dollars */
} invoice_line_t;

/* A complete invoice for one account. */
typedef struct {
    int64_t        generated_at;     /* when the invoice was built */
    money_t        total;            /* sum of all line costs */
    uint32_t       account_id;
    char           account_name[MAX_NAME_LEN];
    invoice_line_t lines[RES_COUNT]; /* one line per resource, indexed by resource_t */
} invoice_t;

/* Build an invoice for one account. Reads the account's name from the
   accounts file at accounts_path, then asks the ledger for each resource's
   total and prices it. Returns BILL_ERR_NOT_FOUND if the account doesn't exist. */
status_t invoice_build(ledger_t *ledger, const char *accounts_path,
                       uint32_t account_id, invoice_t *out);

/* Write the invoice as CSV to an already-open stream (a file or stdout). */
status_t invoice_write_csv(const invoice_t *inv, FILE *out);

/* Write the invoice as JSON to an already-open stream. */
status_t invoice_write_json(const invoice_t *inv, FILE *out);

#endif /* INVOICE_H */
