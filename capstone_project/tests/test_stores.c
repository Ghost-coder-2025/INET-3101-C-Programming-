#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "account.h"
#include "meter.h"

#define ACCOUNTS_FILE "data/test_accounts.dat"
#define METERS_FILE   "data/test_meters.dat"

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

int main(void)
{
    account_store_t *accounts = NULL;
    meter_store_t   *meters = NULL;
    uint32_t id = 0;
    account_t acct;
    meter_t m;
    char longname[70];

    /* Start from empty files so the test gives the same result every time. */
    unlink(ACCOUNTS_FILE);
    unlink(METERS_FILE);

    CHECK(account_store_open(ACCOUNTS_FILE, &accounts) == BILL_OK,
          "open accounts file");
    CHECK(meter_store_open(METERS_FILE, &meters) == BILL_OK,
          "open meters file");
    if (accounts == NULL || meters == NULL) {
        printf("Cannot continue without both files.\n");
        return 1;
    }

    /* ---- Accounts ---- */
    printf("\n-- Accounts --\n");

    CHECK(account_create(accounts, "Acme Corp", &id) == BILL_OK && id == 1,
          "create first account (gets id 1)");
    CHECK(account_create(accounts, "Zeta Labs", &id) == BILL_OK && id == 2,
          "create second account (gets id 2)");
    CHECK(account_create(accounts, "", &id) == BILL_ERR_INVALID,
          "an empty name is rejected");

    memset(longname, 'A', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    CHECK(account_create(accounts, longname, &id) == BILL_ERR_INVALID,
          "a name over 63 characters is rejected");

    CHECK(account_read(accounts, 1, &acct) == BILL_OK &&
          strcmp(acct.name, "Acme Corp") == 0,
          "read account 1");
    CHECK(account_read(accounts, 99, &acct) == BILL_ERR_NOT_FOUND,
          "an unknown account is not found");
    CHECK(account_update(accounts, 2, "Zeta Labs Inc") == BILL_OK &&
          account_read(accounts, 2, &acct) == BILL_OK &&
          strcmp(acct.name, "Zeta Labs Inc") == 0,
          "update account 2's name");
    CHECK(account_delete(accounts, 2) == BILL_OK,
          "delete account 2");
    CHECK(account_read(accounts, 2, &acct) == BILL_ERR_NOT_FOUND,
          "a deleted account is not found by read");
    CHECK(account_read_any(accounts, 2, &acct) == BILL_OK && acct.deleted == 1,
          "a deleted account is still found by read_any");
    CHECK(account_delete(accounts, 2) == BILL_ERR_NOT_FOUND,
          "deleting it a second time is rejected");

    /* ---- Meters (all for account 1) ---- */
    printf("\n-- Meters (account 1) --\n");

    CHECK(meter_create(meters, 1, RES_COMPUTE_CPU) == BILL_OK,
          "create the CPU meter");
    CHECK(meter_create(meters, 1, RES_COMPUTE_CPU) == BILL_ERR_EXISTS,
          "creating the CPU meter twice is rejected");
    CHECK(meter_create(meters, 1, RES_STORAGE) == BILL_OK,
          "create the storage meter");
    CHECK(meter_create(meters, 1, RES_NET_OUT) == BILL_OK,
          "create the outbound network meter");
    CHECK(meter_read(meters, 1, RES_COMPUTE_RAM, &m) == BILL_ERR_NOT_FOUND,
          "a meter that was never created is not found");

    CHECK(meter_apply_usage(meters, 1, RES_COMPUTE_CPU, 100) == BILL_OK &&
          meter_apply_usage(meters, 1, RES_COMPUTE_CPU, 50) == BILL_OK &&
          meter_read(meters, 1, RES_COMPUTE_CPU, &m) == BILL_OK &&
          m.total_quantity == 150,
          "CPU usage adds up (100 + 50 = 150)");

    CHECK(meter_apply_usage(meters, 1, RES_STORAGE, 500) == BILL_OK &&
          meter_apply_usage(meters, 1, RES_STORAGE, 300) == BILL_OK &&
          meter_read(meters, 1, RES_STORAGE, &m) == BILL_OK &&
          m.total_quantity == 500,
          "storage keeps its peak (500, then 300, stays 500)");
    CHECK(meter_apply_usage(meters, 1, RES_STORAGE, 800) == BILL_OK &&
          meter_read(meters, 1, RES_STORAGE, &m) == BILL_OK &&
          m.total_quantity == 800,
          "a higher storage value raises the peak to 800");

    CHECK(meter_apply_usage(meters, 1, RES_COMPUTE_CPU, -5) == BILL_ERR_INVALID,
          "negative usage is rejected");
    CHECK(meter_apply_usage(meters, 1, RES_NET_OUT, INT64_MAX) == BILL_OK &&
          meter_apply_usage(meters, 1, RES_NET_OUT, 1) == BILL_ERR_OVERFLOW,
          "a sum that is too big reports overflow");

    CHECK(meter_set_total(meters, 1, RES_COMPUTE_CPU, 7) == BILL_OK &&
          meter_read(meters, 1, RES_COMPUTE_CPU, &m) == BILL_OK &&
          m.total_quantity == 7,
          "set_total overwrites the CPU total");
    CHECK(meter_delete(meters, 1, RES_COMPUTE_CPU) == BILL_OK &&
          meter_read(meters, 1, RES_COMPUTE_CPU, &m) == BILL_ERR_NOT_FOUND,
          "delete the CPU meter, then it is not found");

    /* ---- Restart: is the data still there? ---- */
    printf("\n-- Restart: close both files, reopen, read back --\n");

    account_store_close(accounts);
    meter_store_close(meters);
    accounts = NULL;
    meters = NULL;

    CHECK(account_store_open(ACCOUNTS_FILE, &accounts) == BILL_OK &&
          meter_store_open(METERS_FILE, &meters) == BILL_OK,
          "reopen both files");
    CHECK(account_read(accounts, 1, &acct) == BILL_OK &&
          strcmp(acct.name, "Acme Corp") == 0,
          "account 1 survived the restart");
    CHECK(meter_read(meters, 1, RES_STORAGE, &m) == BILL_OK &&
          m.total_quantity == 800,
          "the storage meter survived the restart");

    account_store_close(accounts);
    meter_store_close(meters);

    /* ---- Summary ---- */
    if (failures == 0)
        printf("\nAll checks passed.\n");
    else
        printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
