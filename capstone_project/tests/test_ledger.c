#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ledger.h"

#define LOG_FILE "data/test_ledger.log"
#define BAD_FILE "data/test_ledger_bad.log"

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

/* Filled in by the callback that ledger_for_each calls. */
typedef struct {
    ledger_t *ledger;
    int       count;
    int64_t   sum;
    int64_t   first_quantity;
    int       called_ledger_ok;
} walk_t;

static void visit(const ledger_entry_t *entry, void *user_data)
{
    walk_t *walk = user_data;
    int64_t totals[RES_COUNT];

    if (walk->count == 0)
        walk->first_quantity = entry->quantity;
    walk->count++;
    walk->sum += entry->quantity;

    /* Calling the ledger from inside the callback must not freeze. */
    if (ledger_current_totals(walk->ledger, entry->account_id, totals) == BILL_OK)
        walk->called_ledger_ok = 1;
}

int main(void)
{
    ledger_t *ledger = NULL;
    uint64_t seq = 0, as_seq = 0;
    int64_t totals[RES_COUNT];
    time_t now = time(NULL);
    walk_t walk;
    FILE *fp;

    /* Start from empty files so the test gives the same result every time. */
    unlink(LOG_FILE);
    unlink(BAD_FILE);

    CHECK(ledger_create(LOG_FILE, &ledger) == BILL_OK,
          "open a new, empty ledger");
    if (ledger == NULL) {
        printf("Cannot continue without a ledger.\n");
        return 1;
    }

    /* ---- Appending ---- */
    printf("\n-- Appending --\n");

    CHECK(ledger_append(ledger, 1, RES_COMPUTE_CPU, 100, &seq) == BILL_OK &&
          seq == 1, "first entry gets number 1");
    CHECK(ledger_append(ledger, 1, RES_COMPUTE_CPU, 50, &seq) == BILL_OK &&
          seq == 2, "second entry gets number 2");
    CHECK(ledger_append(ledger, 1, RES_STORAGE, 500, &seq) == BILL_OK &&
          seq == 3, "third entry gets number 3");
    CHECK(ledger_append(ledger, 1, RES_STORAGE, 300, &seq) == BILL_OK &&
          seq == 4, "fourth entry gets number 4");
    CHECK(ledger_append(ledger, 1, RES_NET_OUT, 250, &seq) == BILL_OK &&
          seq == 5, "fifth entry gets number 5");
    CHECK(ledger_append(ledger, 2, RES_COMPUTE_CPU, 10, &seq) == BILL_OK &&
          seq == 6, "an entry for account 2 gets number 6");

    CHECK(ledger_append(ledger, 0, RES_NET_OUT, 5, &seq) == BILL_ERR_INVALID,
          "account 0 is rejected");
    CHECK(ledger_append(ledger, 1, RES_NET_OUT, 0, &seq) == BILL_ERR_INVALID,
          "a quantity of 0 is rejected");
    CHECK(ledger_append(ledger, 1, RES_NET_OUT, -5, &seq) == BILL_ERR_INVALID,
          "a negative quantity is rejected");
    CHECK(ledger_append(ledger, 1, RES_NET_OUT, MAX_QUANTITY + 1, &seq) ==
          BILL_ERR_INVALID, "a quantity over the maximum is rejected");
    CHECK(ledger_append(ledger, 1, (resource_t)99, 5, &seq) == BILL_ERR_INVALID,
          "an unknown resource type is rejected");

    /* ---- Totals ---- */
    printf("\n-- Totals --\n");

    CHECK(ledger_current_totals(ledger, 1, totals) == BILL_OK &&
          totals[RES_COMPUTE_CPU] == 150 && totals[RES_STORAGE] == 500 &&
          totals[RES_NET_OUT] == 250 && totals[RES_COMPUTE_RAM] == 0 &&
          totals[RES_NET_IN] == 0,
          "account 1: CPU adds up (150), storage keeps its peak (500), network out is 250");
    CHECK(ledger_current_totals(ledger, 2, totals) == BILL_OK &&
          totals[RES_COMPUTE_CPU] == 10,
          "account 2 only has its own usage");
    CHECK(ledger_current_totals(ledger, 99, totals) == BILL_OK &&
          totals[RES_COMPUTE_CPU] == 0 && totals[RES_NET_OUT] == 0,
          "an account with no usage has zero totals");
    CHECK(ledger_totals(ledger, 1, (int64_t)now - 100, (int64_t)now + 100,
                        totals, &as_seq) == BILL_OK &&
          totals[RES_COMPUTE_CPU] == 150 && as_seq == 6,
          "a period around now counts the entries, and as_seq is 6");
    CHECK(ledger_totals(ledger, 1, 0, 1000, totals, &as_seq) == BILL_OK &&
          totals[RES_COMPUTE_CPU] == 0 && totals[RES_STORAGE] == 0,
          "a period in 1970 counts nothing");
    CHECK(ledger_totals(ledger, 1, 100, 100, totals, &as_seq) ==
          BILL_ERR_INVALID,
          "a period that ends when it starts is rejected");

    /* ---- Walking through entries ---- */
    printf("\n-- Walking through entries --\n");

    memset(&walk, 0, sizeof walk);
    walk.ledger = ledger;
    CHECK(ledger_for_each(ledger, 1, 0, INT64_MAX, visit, &walk) == BILL_OK &&
          walk.count == 5 && walk.sum == 1200 && walk.first_quantity == 100,
          "visits account 1's 5 entries, oldest first");
    CHECK(walk.called_ledger_ok == 1,
          "the callback can call the ledger without freezing");

    memset(&walk, 0, sizeof walk);
    walk.ledger = ledger;
    CHECK(ledger_for_each(ledger, 99, 0, INT64_MAX, visit, &walk) == BILL_OK &&
          walk.count == 0,
          "an account with no entries visits nothing");

    /* ---- Restart ---- */
    printf("\n-- Restart: close, reopen, read back --\n");

    ledger_destroy(ledger);
    ledger = NULL;
    CHECK(ledger_create(LOG_FILE, &ledger) == BILL_OK, "reopen the ledger");
    if (ledger == NULL)
        return 1;
    CHECK(ledger_current_totals(ledger, 1, totals) == BILL_OK &&
          totals[RES_COMPUTE_CPU] == 150 && totals[RES_STORAGE] == 500,
          "the totals survived the restart");
    CHECK(ledger_append(ledger, 2, RES_NET_OUT, 40, &seq) == BILL_OK &&
          seq == 7,
          "the next entry gets number 7 (rejected entries used no numbers)");

    /* ---- A crash in the middle of a write ---- */
    printf("\n-- A crash in the middle of a write --\n");

    ledger_destroy(ledger);
    ledger = NULL;
    fp = fopen(LOG_FILE, "a");
    if (fp != NULL) {
        fputs("8,2,NET_OUT,12", fp);        /* a line with no newline: half written */
        fclose(fp);
    }
    CHECK(ledger_create(LOG_FILE, &ledger) == BILL_OK,
          "a half-written last line does not stop the ledger from opening");
    if (ledger == NULL)
        return 1;
    CHECK(ledger_append(ledger, 2, RES_NET_OUT, 5, &seq) == BILL_OK &&
          seq == 8,
          "the next entry takes number 8");

    ledger_destroy(ledger);
    ledger = NULL;
    CHECK(ledger_create(LOG_FILE, &ledger) == BILL_OK &&
          ledger_current_totals(ledger, 2, totals) == BILL_OK &&
          totals[RES_NET_OUT] == 45,
          "the half-written line was ignored (40 + 5 = 45, not 57)");

    /* ---- Damaged files ---- */
    printf("\n-- Damaged files --\n");

    ledger_destroy(ledger);
    ledger = NULL;

    fp = fopen(BAD_FILE, "w");
    if (fp != NULL) {
        fputs("this is not a ledger line\n", fp);
        fclose(fp);
    }
    CHECK(ledger_create(BAD_FILE, &ledger) == BILL_ERR_CORRUPT &&
          ledger == NULL,
          "a garbage line is reported as corrupt");

    unlink(BAD_FILE);
    fp = fopen(BAD_FILE, "w");
    if (fp != NULL) {
        fputs("1,1,NET_OUT,5\n3,1,NET_OUT,5\n", fp);   /* number 2 is missing */
        fclose(fp);
    }
    CHECK(ledger_create(BAD_FILE, &ledger) == BILL_ERR_CORRUPT &&
          ledger == NULL,
          "a gap in the entry numbers is reported as corrupt");

    ledger_destroy(NULL);
    CHECK(1, "destroying a NULL ledger is safe");
    unlink(BAD_FILE);

    /* ---- Summary ---- */
    if (failures == 0)
        printf("\nAll checks passed.\n");
    else
        printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
