#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pricing.h"

static int failures = 0;

/* Print PASS or FAIL for one check, and count the failures. */
#define CHECK(cond, what)                          \
    do {                                           \
        if (cond) {                                \
            printf("PASS  %s\n", what);            \
        } else {                                   \
            printf("FAIL  %s\n", what);            \
            failures++;                            \
        }                                          \
    } while (0)

/* True if the cost of this usage is exactly "expected" micro-dollars. */
static int cost_is(resource_t type, int64_t quantity, money_t expected)
{
    money_t cost = -1;
    return pricing_cost(type, quantity, &cost) == BILL_OK && cost == expected;
}

/* The result code for this usage, ignoring the cost. */
static status_t cost_status(resource_t type, int64_t quantity)
{
    money_t cost = -1;
    return pricing_cost(type, quantity, &cost);
}

/* True if formatting this amount gives exactly this text. */
static int formats_as(money_t amount, const char *expected)
{
    char buf[64];
    pricing_format_usd(amount, buf, sizeof buf);
    return strcmp(buf, expected) == 0;
}

/* True if rounding this amount to cents gives "expected". */
static int rounds_to(money_t amount, money_t expected)
{
    money_t rounded = -1;
    return pricing_round_to_cents(amount, &rounded) == BILL_OK &&
           rounded == expected;
}

int main(void)
{
    char small[8];
    char big[64];
    money_t cost = 0;

    /* ---- Storage to GB ---- */
    printf("-- Storage to GB --\n");

    CHECK(pricing_storage_gb(0) == 0, "0 MB is 0 GB");
    CHECK(pricing_storage_gb(1023) == 0, "1,023 MB is 0 GB (rounded down)");
    CHECK(pricing_storage_gb(1024) == 1, "1,024 MB is 1 GB");
    CHECK(pricing_storage_gb(2047) == 1, "2,047 MB is still 1 GB (rounded down)");
    CHECK(pricing_storage_gb(2048) == 2, "2,048 MB is 2 GB");
    CHECK(pricing_storage_gb(-5) == 0, "a negative amount is 0 GB");

    /* ---- CPU and RAM ---- */
    printf("\n-- CPU and RAM --\n");

    CHECK(cost_is(RES_COMPUTE_CPU, 0, 0), "0 CPU seconds costs 0");
    CHECK(cost_is(RES_COMPUTE_CPU, 3600, 40000), "1 CPU hour (3,600 s) costs 40,000");
    CHECK(cost_is(RES_COMPUTE_CPU, 36000, 400000), "10 CPU hours costs 400,000");
    CHECK(cost_is(RES_COMPUTE_CPU, 1, 11), "1 CPU second costs 11 (11.1 rounds down)");
    CHECK(cost_is(RES_COMPUTE_RAM, 7200, 10000), "2 RAM hours (7,200 s) costs 10,000");
    CHECK(cost_is(RES_COMPUTE_RAM, 9, 13),
          "RAM for 9 seconds is exactly 12.5, which rounds UP to 13");

    /* ---- Storage cost ---- */
    printf("\n-- Storage cost --\n");

    CHECK(cost_is(RES_STORAGE, 1023, 0), "1,023 MB is under 1 GB, so it costs 0");
    CHECK(cost_is(RES_STORAGE, 2047, 23000), "2,047 MB is 1 GB, so it costs 23,000");
    CHECK(cost_is(RES_STORAGE, 102400, 2300000),
          "100 GB (the end of the first tier) costs 2,300,000");
    CHECK(cost_is(RES_STORAGE, 103424, 2320000),
          "101 GB costs 2,300,000 + 20,000 = 2,320,000");
    CHECK(cost_is(RES_STORAGE, 153600, 3300000),
          "150 GB costs 100 x 23,000 + 50 x 20,000 = 3,300,000");

    /* ---- Network ---- */
    printf("\n-- Network --\n");

    CHECK(cost_is(RES_NET_IN, 0, 0), "inbound network: 0 MB is free");
    CHECK(cost_is(RES_NET_IN, 1000000, 0), "inbound network: 1,000,000 MB is still free");
    CHECK(cost_is(RES_NET_OUT, 500, 0), "outbound: 500 MB is inside the free allowance");
    CHECK(cost_is(RES_NET_OUT, 1000, 0), "outbound: exactly 1,000 MB is still free");
    CHECK(cost_is(RES_NET_OUT, 1001, 90), "outbound: 1,001 MB bills 1 MB at 90");
    CHECK(cost_is(RES_NET_OUT, 2000, 90000), "outbound: 2,000 MB bills 1,000 MB at 90 = 90,000");
    CHECK(cost_is(RES_NET_OUT, 11000, 900000),
          "outbound: 11,000 MB bills 10,000 MB at 90 = 900,000");
    CHECK(cost_is(RES_NET_OUT, 12000, 985000),
          "outbound: 12,000 MB = 900,000 + 1,000 MB at 85 = 985,000");

    /* ---- Bad input ---- */
    printf("\n-- Bad input --\n");

    CHECK(cost_status(RES_NET_OUT, -1) == BILL_ERR_INVALID,
          "a negative quantity is rejected");
    CHECK(cost_status((resource_t)99, 5) == BILL_ERR_INVALID,
          "an unknown resource type is rejected");
    CHECK(pricing_cost(RES_NET_OUT, 5, NULL) == BILL_ERR_INVALID,
          "a missing output pointer is rejected");

    /* ---- Overflow ---- */
    printf("\n-- Overflow --\n");

    CHECK(cost_status(RES_COMPUTE_CPU, INT64_MAX) == BILL_ERR_OVERFLOW,
          "a huge CPU quantity reports overflow");
    CHECK(cost_status(RES_COMPUTE_RAM, INT64_MAX) == BILL_ERR_OVERFLOW,
          "a huge RAM quantity reports overflow");
    CHECK(cost_status(RES_STORAGE, INT64_MAX) == BILL_ERR_OVERFLOW,
          "a huge storage quantity reports overflow");
    CHECK(cost_status(RES_NET_OUT, INT64_MAX) == BILL_ERR_OVERFLOW,
          "a huge outbound quantity reports overflow");
    CHECK(cost_status(RES_NET_IN, INT64_MAX) == BILL_OK,
          "a huge inbound quantity is fine (it is always free)");

    /* ---- Rounding to cents ---- */
    printf("\n-- Rounding to cents --\n");

    CHECK(rounds_to(0, 0), "0 stays 0");
    CHECK(rounds_to(985000, 990000), "98.5 cents rounds up to 99 cents");
    CHECK(rounds_to(984999, 980000), "98.4999 cents rounds down to 98 cents");
    CHECK(rounds_to(5000, 10000), "exactly half a cent rounds up to 1 cent");
    CHECK(rounds_to(4999, 0), "just under half a cent rounds down to 0");
    CHECK(pricing_round_to_cents(-1, &cost) == BILL_ERR_INVALID,
          "a negative amount is rejected");
    CHECK(pricing_round_to_cents(INT64_MAX, &cost) == BILL_ERR_OVERFLOW,
          "rounding a huge amount reports overflow");

    /* ---- Formatting ---- */
    printf("\n-- Formatting --\n");

    CHECK(formats_as(0, "$0.00"), "0 formats as $0.00");
    CHECK(formats_as(990000, "$0.99"), "990,000 formats as $0.99");
    CHECK(formats_as(985000, "$0.99"), "985,000 rounds to the cent and formats as $0.99");
    CHECK(formats_as(1000000, "$1.00"), "1,000,000 formats as $1.00");
    CHECK(formats_as(12340000, "$12.34"), "12,340,000 formats as $12.34");
    CHECK(formats_as(4999, "$0.00"), "4,999 formats as $0.00");
    CHECK(formats_as(5000, "$0.01"), "5,000 formats as $0.01");
    CHECK(formats_as(-1000000, "-$1.00"), "a negative amount gets a minus sign");
    CHECK(formats_as(INT64_MIN, "-$9223372036854.78"),
          "the most negative number formats without breaking");

    memset(small, 'X', sizeof small);
    pricing_format_usd(12340000, small, 4);       /* "$12.34" needs 7 bytes */
    CHECK(strcmp(small, "$12") == 0 && small[4] == 'X',
          "a too-small buffer is cut off and never written past");

    pricing_format_usd(5, NULL, 10);
    pricing_format_usd(5, big, 0);
    CHECK(1, "a missing buffer, or a buffer of size 0, does not crash");

    /* ---- Summary ---- */
    if (failures == 0)
        printf("\nAll checks passed.\n");
    else
        printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
