#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>
#include <endian.h>
#include <argp.h>

#define VERSION 7
#define SA struct sockaddr

struct client_arguments {
    char ip_address[16];
    int port;
    int n_requests;
    int timeout_secs;
    int condensed;
};

static error_t client_parser(int key, char *arg, struct argp_state *state) {
    struct client_arguments *args = state->input;
    switch (key) {
        case 'a':
            strncpy(args->ip_address, arg, sizeof(args->ip_address) - 1);
            args->ip_address[sizeof(args->ip_address)-1] = '\0';
            break;
        case 'p':
            args->port = atoi(arg);
            break;
        case 'n':
            args->n_requests = atoi(arg);
            break;
        case 't':
            args->timeout_secs = atoi(arg);
            break;
        case 'c':
            args->condensed = 1;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct client_arguments client_parseopt(int argc, char *argv[]) {
    static struct argp_option options[] = {
        {"addr", 'a', "addr", 0, "Server IPv4 address", 0},
        {"port", 'p', "port", 0, "Server port", 0},
        {"num", 'n', "N", 0, "Number of requests", 0},
        {"timeout", 't', "T", 0, "Timeout (seconds, 0=forever)", 0},
        {"condensed", 'c', 0, 0, "Use condensed format", 0},
        {0}
    };
    struct argp argp_settings = { options, client_parser, 0, 0 };
    struct client_arguments args;
    memset(&args, 0, sizeof(args));
    argp_parse(&argp_settings, argc, argv, 0, NULL, &args);
    return args;
}

static inline struct timespec now_ts(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

static inline void put_u32(uint8_t *b, uint32_t v) {
    uint32_t n = htonl(v);
    memcpy(b, &n, 4);
}

static inline uint32_t get_u32(const uint8_t *b) {
    uint32_t n;
    memcpy(&n, b, 4);
    return ntohl(n);
}

static inline void put_u64(uint8_t *b, uint64_t v) {
    uint64_t n = htobe64(v);
    memcpy(b, &n, 8);
}

static inline uint64_t get_u64(const uint8_t *b) {
    uint64_t n;
    memcpy(&n, b, 8);
    return be64toh(n);
}

struct __attribute__((__packed__)) condensed_request {
    uint32_t seq_be;
    uint16_t ver_be;
    uint64_t c_sec_be;
    uint64_t c_nsec_be;
};

struct __attribute__((__packed__)) condensed_response {
    uint32_t seq_be;
    uint16_t ver_be;
    uint64_t c_sec_be;
    uint64_t c_nsec_be;
    uint64_t s_sec_be;
    uint64_t s_nsec_be;
};

struct request_record {
    uint64_t c_sec;
    uint64_t c_nsec;
    int received;
    double theta;
    double delta;
};

void orchestrate_client_protocol(int sockfd, struct sockaddr_in *servaddr, int N, int timeout_seconds, int condensed) {
    struct request_record *reqs = calloc(N + 1, sizeof(*reqs));
    if (!reqs) {
        perror("calloc");
        exit(1);
    }
    socklen_t servlen = sizeof(*servaddr);
    for (int seq = 1; seq <= N; seq++) {
        struct timespec t0 = now_ts();
        if (condensed) {
            struct condensed_request req;
            req.seq_be = htonl((uint32_t) seq);
            req.ver_be = htons((uint16_t) VERSION);
            req.c_sec_be = htobe64((uint64_t) t0.tv_sec);
            req.c_nsec_be = htobe64((uint64_t) t0.tv_nsec);
            sendto(sockfd, &req, sizeof(req), 0, (SA *) servaddr, servlen);
        } else {
            uint8_t buf[24];
            put_u32(buf, seq);
            put_u32(buf + 4, VERSION);
            put_u64(buf + 8, (uint64_t) t0.tv_sec);
            put_u64(buf + 16, (uint64_t) t0.tv_nsec);
            sendto(sockfd, buf, sizeof(buf), 0, (SA *) servaddr, servlen);
        }
        reqs[seq].c_sec = t0.tv_sec;
        reqs[seq].c_nsec = t0.tv_nsec;
    }
    int received = 0;
    time_t last_activity = time(NULL);
    while (received < N) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        struct timeval tv, *tvp = NULL;
        if (timeout_seconds > 0) {
            time_t elapsed = time(NULL) - last_activity;
            if (elapsed >= timeout_seconds && received > 0) break;
            tv.tv_sec = timeout_seconds - elapsed;
            tv.tv_usec = 0;
            tvp = &tv;
        }
        int rv = select(sockfd + 1, &rfds, NULL, NULL, tvp);
        if (rv <= 0) {
            if (rv < 0 && errno == EINTR) continue;
            break;
        }
        if (FD_ISSET(sockfd, &rfds)) {
            uint8_t rbuf[64];
            struct sockaddr_in from;
            socklen_t flen = sizeof(from);
            ssize_t n = recvfrom(sockfd, rbuf, sizeof(rbuf), 0, (SA *) &from, &flen);
            if (n <= 0) continue;
            struct timespec t2 = now_ts();
            uint32_t seq;
            uint64_t c_sec, c_nsec, s_sec, s_nsec;
            if (condensed) {
                if (n < (ssize_t) sizeof(struct condensed_response)) continue;
                const struct condensed_response *r = (const struct condensed_response *) rbuf;
                uint16_t ver = ntohs(r->ver_be);
                seq = ntohl(r->seq_be);
                if (ver != VERSION || seq < 1 || seq > (uint32_t) N) continue;
                c_sec = be64toh(r->c_sec_be);
                c_nsec = be64toh(r->c_nsec_be);
                s_sec = be64toh(r->s_sec_be);
                s_nsec = be64toh(r->s_nsec_be);
            } else {
                if (n < 40) continue;
                seq = get_u32(rbuf);
                uint32_t ver = get_u32(rbuf + 4);
                if (ver != VERSION || seq < 1 || seq > (uint32_t) N) continue;
                c_sec = get_u64(rbuf + 8);
                c_nsec = get_u64(rbuf + 16);
                s_sec = get_u64(rbuf + 24);
                s_nsec = get_u64(rbuf + 32);
            }
            if (!reqs[seq].received) {
                double T0 = c_sec + c_nsec / 1e9;
                double T1 = s_sec + s_nsec / 1e9;
                double T2 = t2.tv_sec + t2.tv_nsec / 1e9;
                reqs[seq].theta = ((T1 - T0) + (T1 - T2)) / 2.0;
                reqs[seq].delta = (T2 - T0);
                reqs[seq].received = 1;
                received++;
            }
            last_activity = time(NULL);
        }
    }
    for (int i = 1; i <= N; i++) {
        if (reqs[i].received)
            printf("%d: %.4f %.4f\n", i, reqs[i].theta, reqs[i].delta);
        else
            printf("%d: Dropped\n", i);
    }
    fflush(stdout);
    free(reqs);
}

int main(int argc, char *argv[]) {
    struct client_arguments args = client_parseopt(argc, argv);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(1);
    }
    struct sockaddr_in servaddr = { 0 };
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(args.port);
    inet_pton(AF_INET, args.ip_address, &servaddr.sin_addr);
    orchestrate_client_protocol(sockfd, &servaddr, args.n_requests, args.timeout_secs, args.condensed);
    close(sockfd);
    return 0;
}