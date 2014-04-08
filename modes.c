#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "rtsp.h"
#include "rtsp_server.h"
#include "mdns.h"
#include "modes.h"

static void server_shutdown(void) {
    mdns_unregister();
    rtsp_shutdown_stream();

    // should not.
    shairport_shutdown(1);
}

static void activation_run(int fd) {
    rtsp_conn_info conn;
    memset(&conn, 0, sizeof(conn));

    socklen_t slen = sizeof(conn.remote);

    conn.fd = fd;
    getpeername(fd, (struct sockaddr *)&conn.remote, &slen);
    
    rtsp_serve_loop(&conn);    
}

static void activation_shutdown(void) {
    rtsp_shutdown_stream();
}

static void inetd_init(void) {
    // Close stdout and stderr so we don't accidentally send debug to clients.
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

static void inetd_run(void) {
    activation_run(STDIN_FILENO);
}

static void systemd_run(void) {
    const char *e;
    unsigned long l;
    char *p = NULL;
    
    e = getenv("LISTEN_PID");
    if (!e)
        goto fail;

    errno = 0;
    l = strtoul(e, &p, 10);
    if (errno || !p || p == e || *p || l <= 0)
        goto fail;

    if (getpid() != (pid_t) l)
        goto fail;

    e = getenv("LISTEN_FDS");
    if (!e)
        goto fail;

    errno = 0;
    l = strtoul(e, &p, 10);
    if (errno || !p || p == e || *p || l <= 0)
        goto fail;

    if (l != 1)
        goto fail;

    activation_run(3);

fail:
    die("Missing or invalid socket descriptor");
}

static shairport_mode mode_server = { NULL, rtsp_listen_loop, server_shutdown };
static shairport_mode mode_inetd = { inetd_init, inetd_run, activation_shutdown };
static shairport_mode mode_systemd = { NULL, systemd_run, activation_shutdown };

shairport_mode *mode_find(int *argc, char ***argv)
{
    shairport_mode *mode = NULL;
    if (*argc > 1) {
        if (strcmp("--inetd", (*argv)[1]) == 0) {
            mode = &mode_inetd;
        }
        else if (strcmp("--systemd", (*argv)[1]) == 0) {
            mode = &mode_systemd;
        }
    }

    if (mode) {
        (*argc)--;
        (*argv)++;
    }
    else {
        mode = &mode_server;
    }

    return mode;
}


