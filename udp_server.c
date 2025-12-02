#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <endian.h>
#include <argp.h>

#define VERSION 7
#define MAX_CLIENTS 256
#define TWO_MINUTES 120
#define SA struct sockaddr

struct server_arguments {
    int port;
    int drop_rate;
    int condensed;
};

static error_t server_parser(int key, char *arg, struct argp_state *state) {
    struct server_arguments *a = state->input;
    switch (key) {
        case 'p':
            a->port = atoi(arg);
            break;
        case 'd':
            a->drop_rate = atoi(arg);
            break;
        case 'c':
            a->condensed = 1;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct server_arguments server_parseopt(int argc, char *argv[]) {
    static struct argp_option o[] = {
        {"port", 'p', "port", 0, "Port (>1024)", 0},
        {"drop", 'd', "drop", 0, "Drop % [0-100]", 0},
        {"condensed", 'c', 0, 0, "Use condensed format", 0},
        {0}
    };
    struct argp a = { o, server_parser, 0, 0 };
    struct server_arguments s = { 0 };
    argp_parse(&a, argc, argv, 0, NULL, &s);
    return s;
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

struct client_state {
    struct in_addr addr;
    uint16_t port;
    uint32_t max_seq;
    time_t last_update;
    int active;
};

static struct client_state clients[MAX_CLIENTS];

static struct client_state *get_client_slot(struct sockaddr_in *cli) {
    time_t now = time(NULL);
    uint16_t port = ntohs(cli->sin_port);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].addr.s_addr == cli->sin_addr.s_addr && clients[i].port == port) {
            if (now - clients[i].last_update > TWO_MINUTES) {
                clients[i].max_seq = 0;
            }
            return &clients[i];
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].active = 1;
            clients[i].addr = cli->sin_addr;
            clients[i].port = port;
            clients[i].max_seq = 0;
            clients[i].last_update = now;
            return &clients[i];
        }
    }
    return NULL;
}

void orchestrate_server_protocol(int sockfd, int drop_rate, int condensed) {
    srand(time(NULL));
    while (1) {
        uint8_t buf[64];
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0, (SA *) &cli, &len);
        if (n <= 0) continue;
        if ((rand() % 100) < drop_rate) continue;
        uint32_t seq;
        uint64_t c_sec, c_nsec;
        if (condensed) {
            if (n < (ssize_t) sizeof(struct condensed_request)) continue;
            const struct condensed_request *r = (const struct condensed_request *) buf;
            uint16_t ver = ntohs(r->ver_be);
            if (ver != VERSION) continue;
            seq = ntohl(r->seq_be);
            c_sec = be64toh(r->c_sec_be);
            c_nsec = be64toh(r->c_nsec_be);
        } else {
            if (n < 24) continue;
            seq = get_u32(buf);
            uint32_t ver = get_u32(buf + 4);
            if (ver != VERSION) continue;
            c_sec = get_u64(buf + 8);
            c_nsec = get_u64(buf + 16);
        }
        struct client_state *slot = get_client_slot(&cli);
        if (slot) {
            time_t now = time(NULL);
            if (now - slot->last_update > TWO_MINUTES) slot->max_seq = 0;
            if (slot->max_seq && seq < slot->max_seq) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                printf("%s:%u %u %u\n", ip, ntohs(cli.sin_port), seq, slot->max_seq);
                fflush(stdout);
            }
            if (seq > slot->max_seq) {
                slot->max_seq = seq;
                slot->last_update = now;
            }
        }
        struct timespec t = now_ts();
        if (condensed) {
            struct condensed_response resp;
            resp.seq_be = htonl(seq);
            resp.ver_be = htons((uint16_t) VERSION);
            resp.c_sec_be = htobe64(c_sec);
            resp.c_nsec_be = htobe64(c_nsec);
            resp.s_sec_be = htobe64((uint64_t) t.tv_sec);
            resp.s_nsec_be = htobe64((uint64_t) t.tv_nsec);
            sendto(sockfd, &resp, sizeof(resp), 0, (SA *) &cli, len);
        } else {
            uint8_t resp[40];
            put_u32(resp, seq);
            put_u32(resp + 4, VERSION);
            put_u64(resp + 8, c_sec);
            put_u64(resp + 16, c_nsec);
            put_u64(resp + 24, (uint64_t) t.tv_sec);
            put_u64(resp + 32, (uint64_t) t.tv_nsec);
            sendto(sockfd, resp, sizeof(resp), 0, (SA *) &cli, len);
        }
    }
}

int main(int argc, char *argv[]) {
    struct server_arguments args = server_parseopt(argc, argv);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(1);
    }
    struct sockaddr_in serv = { 0 };
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(args.port);
    if (bind(sockfd, (SA *) &serv, sizeof(serv)) != 0) {
        perror("bind");
        exit(1);
    }
    printf("Server ready on port %d (drop=%d%% condensed=%d)\n", args.port, args.drop_rate, args.condensed);
    fflush(stdout);
    orchestrate_server_protocol(sockfd, args.drop_rate, args.condensed);
    close(sockfd);
    return 0;
}
