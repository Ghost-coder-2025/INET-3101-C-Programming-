#include <stdio.h>
#include <string.h>
#include "invoice.h"
#include "ledger.h"
#include "pricing.h"

#define TEST_LOG "data/test_invoice.log"

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do {                          \
    if (cond) { printf("PASS: %s\n", msg); passed++; } \
    else      { printf("FAIL: %s\n", msg); failed++; } \
} while (0)

/* Read everything written to f back into buf as one string. */
static void read_back(FILE *f, char *buf, size_t size)
{
    size_t got;

    rewind(f);
    got = fread(buf, 1, size - 1, f);
    buf[got] = '\0';
}

int main(void)
{
    ledger_t *ledger = NULL;
    invoice_t inv;
    uint64_t  seq = 0;
    money_t   expect, sum;
    char      buf[4096];
    char      want[128];
    char      long_name[MAX_NAME_LEN + 1];
    FILE     *f;
    int       ok, i;
    status_t  st;

    /* ---------- Setup: fresh ledger with known usage ---------- */
    remove(TEST_LOG);
    st = ledger_create(TEST_LOG, &ledger);
    CHECK(st == BILL_OK, "ledger_create");
    if (st != BILL_OK)
        return 1;

    ok = 1;
    ok &= (ledger_append(ledger, 1, RES_COMPUTE_CPU, 100,  &seq) == BILL_OK);
    ok &= (ledger_append(ledger, 1, RES_COMPUTE_CPU, 200,  &seq) == BILL_OK);
    ok &= (ledger_append(ledger, 1, RES_STORAGE,     500,  &seq) == BILL_OK);
    ok &= (ledger_append(ledger, 1, RES_STORAGE,     2000, &seq) == BILL_OK);
    ok &= (ledger_append(ledger, 1, RES_STORAGE,     1000, &seq) == BILL_OK);
    ok &= (ledger_append(ledger, 1, RES_NET_IN,      5000, &seq) == BILL_OK);
    ok &= (ledger_append(ledger, 1, RES_NET_OUT,     3000, &seq) == BILL_OK);
    /* Account 2 must not show up on account 1's invoice */
    ok &= (ledger_append(ledger, 2, RES_COMPUTE_CPU, 999,  &seq) == BILL_OK);
    CHECK(ok, "appended test usage");

    /* ---------- Building an invoice over all time ---------- */
    st = invoice_build(ledger, 1, "Acme Corp", 0, INT64_MAX, &inv);
    CHECK(st == BILL_OK, "invoice_build succeeds");

    CHECK(inv.account_id == 1, "account id copied");
    CHECK(strcmp(inv.account_name, "Acme Corp") == 0, "account name copied");
    CHECK(inv.period_start == 0 && inv.period_end == INT64_MAX, "period copied");
    CHECK(inv.as_seq == seq, "as_seq is the last ledger entry");

    CHECK(inv.lines[RES_COMPUTE_CPU].quantity == 300,
          "CPU is summed (100 + 200 = 300), account 2 not included");
    CHECK(inv.lines[RES_COMPUTE_RAM].quantity == 0, "RAM unused is 0");
    CHECK(inv.lines[RES_STORAGE].quantity == 2000,
          "storage uses the peak (2000), not the sum (3500)");
    CHECK(inv.lines[RES_NET_IN].quantity == 5000, "NET_IN quantity");
    CHECK(inv.lines[RES_NET_OUT].quantity == 3000, "NET_OUT quantity");
    CHECK(inv.lines[RES_NET_IN].cost == 0, "inbound network is free");

    ok = 1;
    sum = 0;
    for (i = 0; i < RES_COUNT; i++) {
        expect = -1;
        if (pricing_cost((resource_t)i, inv.lines[i].quantity, &expect) != BILL_OK)
            ok = 0;
        if (inv.lines[i].cost != expect)
            ok = 0;
        sum += inv.lines[i].cost;
    }
    CHECK(ok, "every line cost matches pricing_cost");
    CHECK(inv.total == sum, "total is the sum of the line costs");

    /* ---------- A period with no usage ---------- */
    st = invoice_build(ledger, 1, "Acme Corp", 1, 2, &inv);
    CHECK(st == BILL_OK, "empty period builds");
    ok = 1;
    for (i = 0; i < RES_COUNT; i++)
        if (inv.lines[i].quantity != 0 || inv.lines[i].cost != 0)
            ok = 0;
    CHECK(ok && inv.total == 0, "empty period is all zeros");

    /* ---------- Bad input ---------- */
    CHECK(invoice_build(ledger, 1, "x", 10, 10, &inv) == BILL_ERR_INVALID,
          "from == to is rejected");
    CHECK(invoice_build(ledger, 1, "x", 20, 10, &inv) == BILL_ERR_INVALID,
          "from > to is rejected");
    memset(long_name, 'a', MAX_NAME_LEN);
    long_name[MAX_NAME_LEN] = '\0';
    CHECK(invoice_build(ledger, 1, long_name, 0, INT64_MAX, &inv) == BILL_ERR_INVALID,
          "name of 64 characters is rejected");
    CHECK(invoice_build(ledger, 1, "x", 0, INT64_MAX, NULL) == BILL_ERR_INVALID,
          "NULL output is rejected");

    /* ---------- CSV output ---------- */
    st = invoice_build(ledger, 1, "=Bad\"Name", 0, INT64_MAX, &inv);
    f = tmpfile();
    CHECK(f != NULL, "tmpfile for CSV");
    if (f != NULL) {
        CHECK(invoice_write_csv(&inv, f) == BILL_OK, "CSV write succeeds");
        read_back(f, buf, sizeof(buf));
        CHECK(strncmp(buf, "account_id,account_name,period_start,period_end,"
                           "resource,quantity,cost_micro\n", 76) == 0,
              "CSV header row");
        CHECK(strstr(buf, "\"'=Bad\"\"Name\"") != NULL,
              "CSV name quoted, quote doubled, formula neutralized");
        snprintf(want, sizeof(want), ",%s,300,",
                 resource_to_string(RES_COMPUTE_CPU));
        CHECK(strstr(buf, want) != NULL, "CSV has the CPU row");
        CHECK(strstr(buf, ",TOTAL,,") != NULL, "CSV has the TOTAL row");
        fclose(f);
    }

    /* ---------- JSON output ---------- */
    st = invoice_build(ledger, 1, "a\tb\"c\\d", 0, INT64_MAX, &inv);
    f = tmpfile();
    CHECK(f != NULL, "tmpfile for JSON");
    if (f != NULL) {
        CHECK(invoice_write_json(&inv, f) == BILL_OK, "JSON write succeeds");
        read_back(f, buf, sizeof(buf));
        CHECK(strstr(buf, "\"a\\u0009b\\\"c\\\\d\"") != NULL,
              "JSON escapes tab, quote and backslash");
        snprintf(want, sizeof(want), "\"resource\": \"%s\", \"quantity\": 300",
                 resource_to_string(RES_COMPUTE_CPU));
        CHECK(strstr(buf, want) != NULL, "JSON has the CPU line");
        CHECK(strstr(buf, "\"total_micro\": ") != NULL, "JSON has total_micro");
        CHECK(strstr(buf, "\"total_display\": \"$") != NULL, "JSON has total_display");
        fclose(f);
    }

    /* ---------- Cleanup ---------- */
    ledger_destroy(ledger);
    remove(TEST_LOG);

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
