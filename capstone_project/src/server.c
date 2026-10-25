#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "account.h"
#include "invoice.h"
#include "ledger.h"
#include "meter.h"
#include "protocol.h"

#define ACCOUNTS_PATH    "data/accounts.dat"
#define METERS_PATH      "data/meters.dat"
#define LEDGER_PATH      "data/ledger.log"
#define MAX_CLIENTS      16          /* connection limit */
#define RECV_TIMEOUT_SEC 30          /* idle clients are dropped after this */

/* ---------- Part 1: shared state and helpers ---------- */

static account_store_t *g_accounts = NULL;
static meter_store_t   *g_meters   = NULL;
static ledger_t        *g_ledger   = NULL;

/* Set to 1 by Ctrl+C (SIGINT) or SIGTERM. */
static _Atomic sig_atomic_t g_stop = 0;

/* Client slots: the socket of each connected client, or -1 if free.
   g_active counts the running client threads. All three are protected
   by g_clients_lock. */
static int             g_client_fd[MAX_CLIENTS];
static int             g_active = 0;
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_clients_done = PTHREAD_COND_INITIALIZER;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* Send all of data. Returns 0 on success, -1 if the client is gone. */
static int send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, data, len, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        data += n;
        len  -= (size_t)n;
    }
    return 0;
}

/* A growing list of account ids, filled in by account_for_each. */
typedef struct {
    uint32_t *ids;
    size_t    count;
    size_t    cap;
    int       failed;                /* 1 if realloc failed */
} id_list_t;

/* account_for_each callback: just save the id. It must not call any
   account_* function, because the store is locked while it runs. */
static void collect_id(const account_t *account, void *user_data)
{
    id_list_t *list = user_data;

    if (list->failed)
        return;

    if (list->count == list->cap) {
        size_t    new_cap = (list->cap == 0) ? 64 : list->cap * 2;
        uint32_t *grown   = realloc(list->ids, new_cap * sizeof(*grown));

        if (grown == NULL) {
            list->failed = 1;
            return;
        }
        list->ids = grown;
        list->cap = new_cap;
    }
    list->ids[list->count++] = account->id;
}

/* Rebuild every active account's meters from the ledger (the source of
   truth). Called once at startup, before any client connects. */
static status_t rebuild_meters(void)
{
    id_list_t list = { NULL, 0, 0, 0 };
    int64_t   totals[RES_COUNT];
    status_t  st;
    size_t    i;
    int       t;

    /* First collect the ids, so the account lock is not held while
       we work with the ledger and meters (never two locks at once). */
    st = account_for_each(g_accounts, collect_id, &list);
    if (st == BILL_OK && list.failed)
        st = BILL_ERR_NOMEM;

    for (i = 0; st == BILL_OK && i < list.count; i++) {
        st = ledger_current_totals(g_ledger, list.ids[i], totals);

        for (t = 0; st == BILL_OK && t < RES_COUNT; t++) {
            st = meter_set_total(g_meters, list.ids[i], (resource_t)t, totals[t]);

            /* A missing meter (e.g. the admin program crashed after
               creating the account) is created, then set. */
            if (st == BILL_ERR_NOT_FOUND) {
                st = meter_create(g_meters, list.ids[i], (resource_t)t);
                if (st == BILL_OK)
                    st = meter_set_total(g_meters, list.ids[i],
                                         (resource_t)t, totals[t]);
            }
        }
    }

    if (st == BILL_OK)
        fprintf(stderr, "server: rebuilt meters for %zu account(s)\n", list.count);

    free(list.ids);
    return st;
}

/* ---------- Part 2: handling one command ---------- */

/* Send "ERR <reason>\n". Returns 0 on success, -1 if the client is gone. */
static int reply_err(int fd, proto_error_t err)
{
    char out[64];

    protocol_reply_err(out, sizeof(out), err);
    return send_all(fd, out, strlen(out));
}

/* Send "OK <value>\n". Returns 0 on success, -1 if the client is gone. */
static int reply_ok(int fd, int64_t value)
{
    char out[64];

    protocol_reply_ok(out, sizeof(out), value);
    return send_all(fd, out, strlen(out));
}

/* USAGE: one lock at a time, in this order: account, ledger, meter. */
static int do_usage(int fd, const command_t *cmd)
{
    account_t acct;
    uint64_t  seq = 0;
    status_t  st;

    /* 1. The account must exist and not be deleted */
    st = account_read(g_accounts, cmd->account_id, &acct);
    if (st != BILL_OK)
        return reply_err(fd, protocol_error_from_status(st));

    /* 2. Record the event. Once this succeeds, the usage is saved. */
    st = ledger_append(g_ledger, cmd->account_id, cmd->type,
                       cmd->quantity, &seq);
    if (st != BILL_OK)
        return reply_err(fd, protocol_error_from_status(st));

    /* 3. Update the cached total. If this fails, the ledger still has
       the event and the meter is fixed at the next startup, so the
       client still gets OK. */
    st = meter_apply_usage(g_meters, cmd->account_id, cmd->type,
                           cmd->quantity);
    if (st != BILL_OK)
        fprintf(stderr, "server: meter update failed for account %u (%s); "
                        "it will be rebuilt at the next startup\n",
                (unsigned)cmd->account_id, status_to_string(st));

    return reply_ok(fd, (int64_t)seq);
}

/* TOTAL: the cached running total (the peak, for STORAGE). */
static int do_total(int fd, const command_t *cmd)
{
    account_t acct;
    meter_t   meter;
    status_t  st;

    st = account_read(g_accounts, cmd->account_id, &acct);
    if (st != BILL_OK)
        return reply_err(fd, protocol_error_from_status(st));

    st = meter_read(g_meters, cmd->account_id, cmd->type, &meter);
    if (st != BILL_OK)
        return reply_err(fd, protocol_error_from_status(st));

    return reply_ok(fd, meter.total_quantity);
}

/* INVOICE: "OK <length>\n" followed by exactly <length> bytes of JSON. */
static int do_invoice(int fd, const command_t *cmd)
{
    account_t acct;
    invoice_t inv;
    char     *json = NULL;
    size_t    json_len = 0;
    FILE     *mem;
    status_t  st;
    int       rc;

    /* A deleted account can still be invoiced, so use read_any */
    st = account_read_any(g_accounts, cmd->account_id, &acct);
    if (st != BILL_OK)
        return reply_err(fd, protocol_error_from_status(st));

    st = invoice_build(g_ledger, cmd->account_id, acct.name,
                       cmd->from, cmd->to, &inv);
    if (st != BILL_OK)
        return reply_err(fd, protocol_error_from_status(st));

    /* Write the JSON into memory first, so we know its length
       before sending anything */
    mem = open_memstream(&json, &json_len);
    if (mem == NULL)
        return reply_err(fd, PROTO_ERR_INTERNAL);

    st = invoice_write_json(&inv, mem);
    if (fclose(mem) != 0 && st == BILL_OK)
        st = BILL_ERR_IO;

    if (st != BILL_OK) {
        free(json);
        return reply_err(fd, protocol_error_from_status(st));
    }

    rc = reply_ok(fd, (int64_t)json_len);
    if (rc == 0)
        rc = send_all(fd, json, json_len);

    free(json);
    return rc;
}

/* Parse and run one line. Returns 0 to keep going, 1 after QUIT,
   or -1 if the client is gone. */
static int handle_line(int fd, const char *line)
{
    command_t     cmd;
    proto_error_t perr;
    char          out[64];

    perr = protocol_parse(line, &cmd);
    if (perr != PROTO_OK)
        return reply_err(fd, perr);

    switch (cmd.kind) {
    case CMD_USAGE:
        return do_usage(fd, &cmd);
    case CMD_TOTAL:
        return do_total(fd, &cmd);
    case CMD_INVOICE:
        return do_invoice(fd, &cmd);
    case CMD_QUIT:
        protocol_reply_bye(out, sizeof(out));
        send_all(fd, out, strlen(out));
        return 1;
    }

    return reply_err(fd, PROTO_ERR_INTERNAL);
}

/* ---------- Part 3: connections and main ---------- */

/* One thread per client. arg is the client's slot number. */
static void *client_thread(void *arg)
{
    int            slot = (int)(intptr_t)arg;
    int            fd;
    conn_reader_t *reader = NULL;
    char           line[MAX_LINE_LEN];
    int            done = 0;

    pthread_mutex_lock(&g_clients_lock);
    fd = g_client_fd[slot];
    pthread_mutex_unlock(&g_clients_lock);

    if (conn_reader_create(fd, &reader) != BILL_OK)
        done = 1;

    while (!done && !g_stop) {
        switch (conn_reader_read_line(reader, line, sizeof(line))) {
        case READ_LINE_OK:
            if (handle_line(fd, line) != 0)
                done = 1;            /* QUIT, or the client is gone */
            break;
        case READ_LINE_TOO_LONG:
            if (reply_err(fd, PROTO_ERR_LINE_TOO_LONG) != 0)
                done = 1;
            break;
        case READ_LINE_BAD:
            if (reply_err(fd, PROTO_ERR_BAD_COMMAND) != 0)
                done = 1;
            break;
        case READ_LINE_EOF:          /* client disconnected */
        case READ_LINE_ERROR:        /* read failed or timed out */
            done = 1;
            break;
        }
    }

    conn_reader_destroy(reader);

    /* Free the slot. close() happens under the lock so the shutdown code
       never touches an fd number that has already been closed. */
    pthread_mutex_lock(&g_clients_lock);
    close(fd);
    g_client_fd[slot] = -1;
    g_active--;
    pthread_cond_signal(&g_clients_done);
    pthread_mutex_unlock(&g_clients_lock);

    return NULL;
}

/* Give a new connection a slot and a thread, or turn it away. */
static void start_client(int fd)
{
    pthread_attr_t attr;
    pthread_t      tid;
    int            slot = -1;
    int            i;

    pthread_mutex_lock(&g_clients_lock);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_fd[i] < 0) {
            slot = i;
            g_client_fd[i] = fd;
            g_active++;
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_lock);

    if (slot < 0) {                  /* all slots in use */
        reply_err(fd, PROTO_ERR_BUSY);
        close(fd);
        return;
    }

    /* Detached: the thread cleans up after itself, nobody joins it */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&tid, &attr, client_thread, (void *)(intptr_t)slot) != 0) {
        pthread_mutex_lock(&g_clients_lock);
        close(fd);
        g_client_fd[slot] = -1;
        g_active--;
        pthread_mutex_unlock(&g_clients_lock);
        fprintf(stderr, "server: could not start a client thread\n");
    }

    pthread_attr_destroy(&attr);
}

int main(void)
{
    struct sigaction   sa;
    struct sockaddr_in addr;
    struct timeval     timeout;
    int                listen_fd = -1;
    int                client_fd;
    int                opt = 1;
    int                exit_code = 1;
    int                i;
    status_t           st;

    for (i = 0; i < MAX_CLIENTS; i++)
        g_client_fd[i] = -1;

    /* A client that disconnects mid-reply must not kill the server */
    signal(SIGPIPE, SIG_IGN);

    /* Ctrl+C and SIGTERM ask for a clean shutdown */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Open the stores and the ledger */
    st = account_store_open(ACCOUNTS_PATH, &g_accounts);
    if (st == BILL_OK)
        st = meter_store_open(METERS_PATH, &g_meters);
    if (st == BILL_OK)
        st = ledger_create(LEDGER_PATH, &g_ledger);
    if (st != BILL_OK) {
        fprintf(stderr, "server: startup failed (%s)\n", status_to_string(st));
        goto cleanup;
    }

    st = rebuild_meters();
    if (st != BILL_OK) {
        fprintf(stderr, "server: meter rebuild failed (%s)\n", status_to_string(st));
        goto cleanup;
    }

    /* Listen on 127.0.0.1 only: there is no authentication, so nothing
       outside this machine should be able to connect */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("server: socket");
        goto cleanup;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("server: bind");
        goto cleanup;
    }
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("server: listen");
        goto cleanup;
    }

    fprintf(stderr, "server: listening on 127.0.0.1:%d (Ctrl+C to stop)\n",
            SERVER_PORT);

    /* Accept loop. poll() wakes up at least once a second so Ctrl+C is
       noticed even when nobody is connecting. */
    while (!g_stop) {
        struct pollfd pfd;
        int           rc;

        pfd.fd      = listen_fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        rc = poll(&pfd, 1, 1000);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            perror("server: poll");
            break;
        }
        if (rc == 0)
            continue;                /* no one connecting, check g_stop again */

        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0)
            continue;

        /* Idle clients are dropped after RECV_TIMEOUT_SEC seconds */
        timeout.tv_sec  = RECV_TIMEOUT_SEC;
        timeout.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        start_client(client_fd);
    }

    fprintf(stderr, "\nserver: shutting down\n");
    exit_code = 0;

cleanup:
    if (listen_fd >= 0)
        close(listen_fd);

    /* Wake every client thread (their read() returns at once) and wait
       until all of them have finished, so no thread is still inside the
       ledger when it is destroyed */
    pthread_mutex_lock(&g_clients_lock);
    for (i = 0; i < MAX_CLIENTS; i++)
        if (g_client_fd[i] >= 0)
            shutdown(g_client_fd[i], SHUT_RDWR);
    while (g_active > 0)
        pthread_cond_wait(&g_clients_done, &g_clients_lock);
    pthread_mutex_unlock(&g_clients_lock);

    ledger_destroy(g_ledger);
    meter_store_close(g_meters);
    account_store_close(g_accounts);

    fprintf(stderr, "server: stopped\n");
    return exit_code;
}
