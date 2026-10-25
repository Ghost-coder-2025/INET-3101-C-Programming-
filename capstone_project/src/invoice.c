#include <stdio.h>
#include <string.h>
#include <time.h>
#include "invoice.h"
#include "pricing.h"

/* ---------- Part 1: building the invoice ---------- */

status_t invoice_build(ledger_t *ledger, uint32_t account_id,
                       const char *account_name, int64_t from, int64_t to,
                       invoice_t *out)
{
    invoice_t inv;
    int64_t   totals[RES_COUNT];
    uint64_t  as_seq = 0;
    status_t  st;
    int       i;

    /* Check the input */
    if (ledger == NULL || account_name == NULL || out == NULL)
        return BILL_ERR_INVALID;
    if (from >= to)
        return BILL_ERR_INVALID;
    if (strlen(account_name) >= MAX_NAME_LEN)
        return BILL_ERR_INVALID;

    memset(&inv, 0, sizeof(inv));

    /* One call gets all five usage totals for the period */
    st = ledger_totals(ledger, account_id, from, to, totals, &as_seq);
    if (st != BILL_OK)
        return st;

    /* Price each resource and add it to the total */
    for (i = 0; i < RES_COUNT; i++) {
        money_t cost = 0;

        st = pricing_cost((resource_t)i, totals[i], &cost);
        if (st != BILL_OK)
            return st;

        if (cost > INT64_MAX - inv.total)
            return BILL_ERR_OVERFLOW;

        inv.lines[i].quantity = totals[i];
        inv.lines[i].cost     = cost;
        inv.total            += cost;
    }

    /* Fill in the rest */
    inv.generated_at = (int64_t)time(NULL);
    inv.period_start = from;
    inv.period_end   = to;
    inv.as_seq       = as_seq;
    inv.account_id   = account_id;
    memcpy(inv.account_name, account_name, strlen(account_name) + 1);

    /* Only copy to the caller once everything worked */
    *out = inv;
    return BILL_OK;
}

/* ---------- Part 2: CSV output ---------- */

/* Write the account name as a quoted CSV field. Any " inside is doubled,
   and a leading = + - or @ gets a ' in front so spreadsheets don't treat
   it as a formula. Returns 0 on success, -1 if a write fails. */
static int csv_write_name(const char *name, FILE *out)
{
    const char *p;

    if (fputc('"', out) == EOF)
        return -1;

    if (name[0] == '=' || name[0] == '+' || name[0] == '-' || name[0] == '@') {
        if (fputc('\'', out) == EOF)
            return -1;
    }

    for (p = name; *p != '\0'; p++) {
        if (*p == '"') {
            if (fputc('"', out) == EOF)
                return -1;
        }
        if (fputc(*p, out) == EOF)
            return -1;
    }

    if (fputc('"', out) == EOF)
        return -1;
    return 0;
}

/* Write the columns every row starts with: id, name, start, end.
   Returns 0 on success, -1 if a write fails. */
static int csv_write_row_start(const invoice_t *inv, FILE *out)
{
    if (fprintf(out, "%u,", (unsigned)inv->account_id) < 0)
        return -1;
    if (csv_write_name(inv->account_name, out) != 0)
        return -1;
    if (fprintf(out, ",%lld,%lld,",
                (long long)inv->period_start,
                (long long)inv->period_end) < 0)
        return -1;
    return 0;
}

status_t invoice_write_csv(const invoice_t *inv, FILE *out)
{
    int i;

    if (inv == NULL || out == NULL)
        return BILL_ERR_INVALID;

    /* Header row */
    if (fprintf(out, "account_id,account_name,period_start,period_end,"
                     "resource,quantity,cost_micro\n") < 0)
        return BILL_ERR_IO;

    /* One row per resource */
    for (i = 0; i < RES_COUNT; i++) {
        if (csv_write_row_start(inv, out) != 0)
            return BILL_ERR_IO;
        if (fprintf(out, "%s,%lld,%lld\n",
                    resource_to_string((resource_t)i),
                    (long long)inv->lines[i].quantity,
                    (long long)inv->lines[i].cost) < 0)
            return BILL_ERR_IO;
    }

    /* Total row (quantity left empty) */
    if (csv_write_row_start(inv, out) != 0)
        return BILL_ERR_IO;
    if (fprintf(out, "TOTAL,,%lld\n", (long long)inv->total) < 0)
        return BILL_ERR_IO;

    if (fflush(out) == EOF)
        return BILL_ERR_IO;
    return BILL_OK;
}

/* ---------- Part 3: JSON output ---------- */

/* Write the account name as a JSON string. Escapes " and \, and writes
   control characters (below 0x20) as \u00XX. Returns 0 on success, -1 if
   a write fails. */
static int json_write_name(const char *name, FILE *out)
{
    const unsigned char *p;

    if (fputc('"', out) == EOF)
        return -1;

    for (p = (const unsigned char *)name; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            if (fprintf(out, "\\%c", *p) < 0)
                return -1;
        } else if (*p < 0x20) {
            if (fprintf(out, "\\u%04x", *p) < 0)
                return -1;
        } else {
            if (fputc(*p, out) == EOF)
                return -1;
        }
    }

    if (fputc('"', out) == EOF)
        return -1;
    return 0;
}

status_t invoice_write_json(const invoice_t *inv, FILE *out)
{
    char display[32];
    int  i;

    if (inv == NULL || out == NULL)
        return BILL_ERR_INVALID;

    /* Account and period info */
    if (fprintf(out, "{\n  \"account_id\": %u,\n  \"account_name\": ",
                (unsigned)inv->account_id) < 0)
        return BILL_ERR_IO;
    if (json_write_name(inv->account_name, out) != 0)
        return BILL_ERR_IO;
    if (fprintf(out, ",\n"
                     "  \"period_start\": %lld,\n"
                     "  \"period_end\": %lld,\n"
                     "  \"generated_at\": %lld,\n"
                     "  \"as_seq\": %llu,\n"
                     "  \"lines\": [\n",
                (long long)inv->period_start,
                (long long)inv->period_end,
                (long long)inv->generated_at,
                (unsigned long long)inv->as_seq) < 0)
        return BILL_ERR_IO;

    /* One object per resource; comma after every one except the last */
    for (i = 0; i < RES_COUNT; i++) {
        if (fprintf(out,
                    "    {\"resource\": \"%s\", \"quantity\": %lld, "
                    "\"cost_micro\": %lld}%s\n",
                    resource_to_string((resource_t)i),
                    (long long)inv->lines[i].quantity,
                    (long long)inv->lines[i].cost,
                    (i < RES_COUNT - 1) ? "," : "") < 0)
            return BILL_ERR_IO;
    }

    /* Total, both exact and as a display string */
    pricing_format_usd(inv->total, display, sizeof(display));
    if (fprintf(out, "  ],\n"
                     "  \"total_micro\": %lld,\n"
                     "  \"total_display\": \"%s\"\n"
                     "}\n",
                (long long)inv->total, display) < 0)
        return BILL_ERR_IO;

    if (fflush(out) == EOF)
        return BILL_ERR_IO;
    return BILL_OK;
}
