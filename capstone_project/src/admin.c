#include <stdio.h>
#include <string.h>
#include "account.h"
#include "meter.h"

#define ACCOUNTS_PATH "data/accounts.dat"
#define METERS_PATH   "data/meters.dat"

static void print_usage(void)
{
    fprintf(stderr,
            "usage:\n"
            "  billing_admin create <name>\n"
            "  billing_admin list\n"
            "  billing_admin rename <id> <new name>\n"
            "  billing_admin delete <id>\n");
}

/* Parse an account id: digits only, 1 to 4294967295.
   Returns 0 on success, -1 if it is not a valid id. */
static int parse_id(const char *s, uint32_t *out)
{
    int64_t v = 0;

    if (*s == '\0')
        return -1;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
        if (v > 4294967295LL)
            return -1;
    }
    if (v < 1)
        return -1;

    *out = (uint32_t)v;
    return 0;
}

/* account_for_each callback: print one line per active account. */
static void print_account(const account_t *account, void *user_data)
{
    (void)user_data;
    printf("%u\t%s\n", (unsigned)account->id, account->name);
}

int main(int argc, char **argv)
{
    account_store_t *accounts = NULL;
    meter_store_t   *meters   = NULL;
    uint32_t         id = 0;
    status_t         st;
    int              t;
    int              rc = 1;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    st = account_store_open(ACCOUNTS_PATH, &accounts);
    if (st == BILL_OK)
        st = meter_store_open(METERS_PATH, &meters);
    if (st != BILL_OK) {
        fprintf(stderr, "admin: could not open the data files (%s)\n",
                status_to_string(st));
        goto done;
    }

    if (strcmp(argv[1], "create") == 0 && argc == 3) {
        st = account_create(accounts, argv[2], &id);

        /* Spec: all five meters are created with the account */
        for (t = 0; st == BILL_OK && t < RES_COUNT; t++) {
            st = meter_create(meters, id, (resource_t)t);
            if (st == BILL_ERR_EXISTS)
                st = BILL_OK;
        }
        if (st == BILL_OK)
            printf("created account %u\n", (unsigned)id);

    } else if (strcmp(argv[1], "list") == 0 && argc == 2) {
        st = account_for_each(accounts, print_account, NULL);

    } else if (strcmp(argv[1], "rename") == 0 && argc == 4) {
        if (parse_id(argv[2], &id) != 0)
            st = BILL_ERR_INVALID;
        else
            st = account_update(accounts, id, argv[3]);
        if (st == BILL_OK)
            printf("renamed account %u\n", (unsigned)id);

    } else if (strcmp(argv[1], "delete") == 0 && argc == 3) {
        if (parse_id(argv[2], &id) != 0)
            st = BILL_ERR_INVALID;
        else
            st = account_delete(accounts, id);
        if (st == BILL_OK)
            printf("deleted account %u (it can still be invoiced)\n",
                   (unsigned)id);

    } else {
        print_usage();
        goto done;
    }

    if (st == BILL_OK)
        rc = 0;
    else
        fprintf(stderr, "admin: failed (%s)\n", status_to_string(st));

done:
    meter_store_close(meters);
    account_store_close(accounts);
    return rc;
}
