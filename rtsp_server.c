#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <openssl/md5.h>

#include "common.h"
#include "rtsp.h"
#include "player.h"
#include "rtp.h"

#ifdef AF_INET6
#define INETx_ADDRSTRLEN INET6_ADDRSTRLEN
#else
#define INETx_ADDRSTRLEN INET_ADDRSTRLEN
#endif

typedef struct {
    rtsp_conn_info conn;

    int running;
    pthread_t thread;
} rtsp_thread;

// keep track of the threads we have spawned so we can join() them
static rtsp_thread **threads = NULL;
static int nthreads = 0;
static void track_thread(rtsp_thread *t) {
    threads = realloc(threads, sizeof(rtsp_thread*) * (nthreads + 1));
    threads[nthreads] = t;
    nthreads++;
}

static void cleanup_threads(void) {
    void *retval;
    int i;
    debug(2, "culling threads.\n");
    for (i=0; i<nthreads; ) {
        if (threads[i]->running == 0) {
            pthread_join(threads[i]->thread, &retval);
            free(threads[i]);
            debug(2, "one joined\n");
            nthreads--;
            if (nthreads)
                threads[i] = threads[nthreads];
        } else {
            i++;
        }
    }
}


static void *rtsp_conversation_thread_func(void *threadp) {
    // SIGUSR1 is used to interrupt this thread if blocked for read
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    rtsp_thread *thread = threadp;

    rtsp_serve_loop(&thread->conn);

    thread->running = 0;

    debug(2, "terminating RTSP thread\n");

    return NULL;
}



// this function is not thread safe.
static const char* format_address(struct sockaddr *fsa) {
    static char string[INETx_ADDRSTRLEN];
    void *addr;
#ifdef AF_INET6
    if (fsa->sa_family == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)(fsa);
        addr = &(sa6->sin6_addr);
    } else
#endif
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)(fsa);
        addr = &(sa->sin_addr);
    }
    return inet_ntop(fsa->sa_family, addr, string, sizeof(string));
}


void rtsp_listen_loop(void) {
    struct addrinfo hints, *info, *p;
    char portstr[6];
    int *sockfd = NULL;
    int nsock = 0;
    int i, ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(portstr, 6, "%d", config.port);

    ret = getaddrinfo(NULL, portstr, &hints, &info);
    if (ret) {
        die("getaddrinfo failed: %s", gai_strerror(ret));
    }

    for (p=info; p; p=p->ai_next) {
        int fd = socket(p->ai_family, p->ai_socktype, IPPROTO_TCP);
        int yes = 1;

        ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef IPV6_V6ONLY
        // some systems don't support v4 access on v6 sockets, but some do.
        // since we need to account for two sockets we might as well
        // always.
        if (p->ai_family == AF_INET6)
            ret |= setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
#endif

        if (!ret)
            ret = bind(fd, p->ai_addr, p->ai_addrlen);

        // one of the address families will fail on some systems that
        // report its availability. do not complain.
        if (ret) {
            debug(1, "Failed to bind to address %s\n", format_address(p->ai_addr));
            continue;
        }
        debug(1, "Bound to address %s\n", format_address(p->ai_addr));

        listen(fd, 5);
        nsock++;
        sockfd = realloc(sockfd, nsock*sizeof(int));
        sockfd[nsock-1] = fd;
    }

    freeaddrinfo(info);

    if (!nsock)
        die("could not bind any listen sockets!");


    int maxfd = -1;
    fd_set fds;
    FD_ZERO(&fds);
    for (i=0; i<nsock; i++) {
        if (sockfd[i] > maxfd)
            maxfd = sockfd[i];
    }

    mdns_register();

    printf("Listening for connections.\n");
    shairport_startup_complete();

    int acceptfd;
    struct timeval tv;
    while (1) {
        tv.tv_sec = 300;
        tv.tv_usec = 0;

        for (i=0; i<nsock; i++)
            FD_SET(sockfd[i], &fds);

        ret = select(maxfd+1, &fds, 0, 0, &tv);
        if (ret<0) {
            if (errno==EINTR)
                continue;
            break;
        }

        cleanup_threads();

        acceptfd = -1;
        for (i=0; i<nsock; i++) {
            if (FD_ISSET(sockfd[i], &fds)) {
                acceptfd = sockfd[i];
                break;
            }
        }
        if (acceptfd < 0) // timeout
            continue;

        rtsp_thread *thread = malloc(sizeof(rtsp_thread));
        memset(thread, 0, sizeof(rtsp_thread));
        socklen_t slen = sizeof(thread->conn.remote);

        debug(1, "new RTSP connection\n");
        thread->conn.fd = accept(acceptfd, (struct sockaddr *)&thread->conn.remote, &slen);
        if (thread->conn.fd < 0) {
            perror("failed to accept connection");
            free(thread);
        } else {
            ret = pthread_create(&thread->thread, NULL, rtsp_conversation_thread_func, thread);
            if (ret)
                die("Failed to create RTSP receiver thread!");

            thread->running = 1;
            track_thread(thread);
        }
    }
    perror("select");
    die("fell out of the RTSP select loop");
}

