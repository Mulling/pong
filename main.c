// See LICENSE for license details
#define _GNU_SOURCE // for NI_MAXHOST apparently
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <linux/icmp.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

const char *dha = NULL; // destiny host name

uint32_t ttl = 64;
uint64_t seq = 1;
uint64_t sentnum = 0;
uint64_t recvnum = 0;

// time is stored in usec
suseconds_t hrtt = 0;
suseconds_t lrtt = 0x7FFFFFFF;
suseconds_t ortt = 0;
suseconds_t nrtt = 0;

#define MAXDUP 0x1000 // from ping.c
#define PAYLOADSIZE 56
#define RCVTIMEO 1

uint8_t recvtable[MAXDUP / 8] = {0};

bool r = true; // running
bool f = false;

#define PACKETSIZE sizeof(struct iphdr) + sizeof(struct icmphdr) + PAYLOADSIZE
#define ICMPOFF sizeof(struct iphdr)
#define TIMEOFF ICMPOFF + sizeof(struct icmphdr)

uint8_t packetreq[PACKETSIZE];
uint8_t packetrep[PACKETSIZE];

struct iphdr *ip = (struct iphdr*)packetreq;
struct icmphdr *icmp = (struct icmphdr*)(packetreq + ICMPOFF);
struct timeval *timestamp = (struct timeval*)(packetreq + TIMEOFF);

struct iphdr *iprep = (struct iphdr*)packetrep;
struct icmphdr *icmprep = (struct icmphdr*)(packetrep + ICMPOFF);
struct timeval *timestamprep = (struct timeval*)(packetrep + TIMEOFF);

static
uint32_t(*sleepfn)(uint32_t) = sleep;

static
uint32_t nullfn(uint32_t x){ (void)x; return 0; };

static
void usage(void){
    fprintf(stderr,
            "usage:\n"
            "\t./pong [target] [[-f] [-t time_to_live]]\n");
}

// source: https://datatracker.ietf.org/doc/html/rfc1071
static inline
uint16_t checksum(const uint16_t *buffer, size_t len){
    register uint64_t sum = 0;

    while (len > 1){
        sum += *buffer++;
        len -= 2;
    }

    if (len) sum += *(uint8_t*)buffer;
    sum = (sum & 0xFFFF) + (sum >> 16);
    sum += (sum >> 16);

    return (uint16_t)~sum;
}

#ifdef DEBUG
#define ERROR(msg)                                                  \
        fprintf(stderr, __FILE__":%d error: %s \n", __LINE__, msg); \
        exit(1);
#else
#define ERROR(msg)                                 \
        fprintf(stderr, "pong: error: %s\n", msg); \
        exit(1);
#endif

static
const char *get_host_addr(const char *hname){
    static char host[NI_MAXHOST] = {0};

    struct addrinfo h;
    struct addrinfo *res;

    memset(&h, 0, sizeof(struct addrinfo));
    h.ai_family = AF_INET; // for ipv4

    uint32_t ret;

    if ((ret = getaddrinfo(hname, NULL, &h, &res))){ ERROR(gai_strerror(ret)); }

    // get only the first address (if multiple are returned)
    getnameinfo(res->ai_addr, res->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST);

    return host; // this is fine because we only run this one time
}

static
int initsock(uint16_t pid){
    int socketd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socketd < 0){ ERROR("Fail to create a socket, run with root privileges..."); }

    int on = 1;
	if (setsockopt(socketd, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) == -1){
        ERROR("setsockopt IP_HDRINCL");
	}

    struct timeval timeo;
    timeo.tv_sec = f ? 0 : RCVTIMEO;
    timeo.tv_usec = 1;
    if (setsockopt(socketd, SOL_SOCKET, SO_RCVTIMEO, &timeo, sizeof(timeo)) == -1){
        ERROR("setsockopt SO_RCVTIMEO");
    }
    timeo.tv_usec = 0;
    if (setsockopt(socketd, SOL_SOCKET, SO_SNDTIMEO, &timeo, sizeof(timeo)) == -1){
        ERROR("setsockopt SO_SNDTIMEO");
    }

    struct icmp_filter filter;
    filter.data = ~((1<<ICMP_DEST_UNREACH) | (1<<ICMP_TIME_EXCEEDED) |(1<<ICMP_ECHOREPLY));
    if (setsockopt(socketd, SOL_RAW, ICMP_FILTER, &filter, sizeof(filter)) == -1){
        ERROR("setsockopt ICMP_FILTER");
    }

    (void)(pid);
    // TODO: figure this out
    // struct sock_fprog idfilter;
    // setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof filter);

    return socketd;
}

static inline
bool checkdup(const uint16_t bit){
    if (recvtable[bit >> 3] & (1 << (bit & 0x07))) return true;
    recvtable[bit >> 3] |= (1 << (bit & 0x07));
    return false;
}

static inline
void calcrtt(const struct timeval *timestamprep){
    struct timeval timedif;
    struct timeval timenow;

    gettimeofday(&timenow, NULL);
    timersub(&timenow, timestamprep, &timedif); // use the time stored in the packet
    nrtt = timedif.tv_sec * 10000 + timedif.tv_usec;

    if (hrtt < nrtt) hrtt = nrtt;
    if (lrtt > nrtt) lrtt = nrtt;
    if (!ortt){
        ortt = nrtt;
    }
    else{
        ortt = (suseconds_t)((7/8.) * (float)ortt) + ((float)nrtt * (1/8.));
    }
}

static inline
void initiphdr(const in_addr_t daddr){
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(PAYLOADSIZE);
    ip->protocol = IPPROTO_ICMP;
    ip->ttl = ttl;
    ip->daddr = daddr;
}

static inline
void initicmphdr(uint16_t id){
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.sequence = htons((uint16_t)seq);
    icmp->un.echo.id = id;
}

static inline
bool chkchksum(struct icmphdr *icmprep){
    uint16_t chksum = icmprep->checksum;
    icmprep->checksum = 0;
    return chksum == checksum((const uint16_t*)icmprep, sizeof(struct icmphdr) + PAYLOADSIZE);
}

// source: RFC 792
static const char *icmp_dest_unreach_code[] = {
    [0] = "net unreachable",
    [1] = "host unreachable",
    [2] = "protocol unreachable",
    [3] = "port unreachable",
    [4] = "fragmentation needed and DF set",
    [5] = "source route failed",
    ""
};

// source: RFC 792
static const char *icmp_time_exceeded_code[] = {
    [0] = "time to live exceeded in transit",
    [1] = "fragment reassembly time exceeded",
    ""
};

void ping(const in_addr_t daddr){
    pid_t pid = getpid();
    uint16_t id = (uint16_t)(pid >> 16) ^ (pid & 0xFF);
    int socketd = initsock(id);

    initiphdr(daddr);
    initicmphdr(id);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = daddr;
    memset(&servaddr.sin_zero, 0, sizeof(servaddr.sin_zero));

    int32_t sentsize = 0;
    int32_t recvsize = 0;

    fprintf(stdout, "PONGING %s %d bytes of data (%lu total).\n",
            dha, PAYLOADSIZE, PACKETSIZE); fflush(stdout);

    while (r){
        recvtable[((sentnum + 1) % MAXDUP) >> 3] &= ~(1 << (((sentnum + 1) % MAXDUP) & 0x07));
        gettimeofday(timestamp, NULL);
        icmp->checksum = 0;
        icmp->checksum = checksum((uint16_t*)icmp, sizeof(struct icmphdr) + PAYLOADSIZE);
        // TODO: replace sendto and recvfrom with sendmsg and recvmsg :)
        if ((sentnum++, sentsize = sendto(socketd, packetreq, PACKETSIZE, 0,
                        (struct sockaddr*)&servaddr, sizeof(servaddr))) < 1){
            sleepfn(RCVTIMEO);
            continue; // if we can't send keep trying
        }
recv:
        if ((recvsize = recvfrom(socketd, packetrep, PACKETSIZE, MSG_WAITALL, NULL, NULL)) == -1
                && !(errno & EINTR)
                && recvsize != PACKETSIZE){
            // we didn't get a reply in time send again with the same seq, if flooding don't wait
            if (f) goto incs; else continue;
        }
        else if (errno & EINTR){
            // FIXME: we should (AT LEAST) try to get all the packets we've sent before terminating
            // because we can get some trash that was sent by other service, and we didn't get all
            // our packets. This will also cause packet loss when flooding.
            --sentnum; // if we got were recvfrom was interrupted, ignore the last package we sent
            break;
        }

        if (!chkchksum(icmprep)){
            fprintf(stdout, "\x1b[5mBAD CHECKSUM!!!\n\x1b[m");
            goto chkf;
        }

        switch ((++recvnum, icmprep->type)){
            case ICMP_ECHOREPLY:
                // got something that is not ours
                if (icmprep->un.echo.id != id){ --recvnum; goto recv; }
                calcrtt(timestamprep);
                fprintf(stdout, "%d bytes from %s icmp_seq=%d ttl=%u time=%.1fms",
                        recvsize, dha, ntohs(icmprep->un.echo.sequence), iprep->ttl, nrtt/1000.0);
                fprintf(stdout,
                        (checkdup(htons(icmprep->un.echo.sequence) % MAXDUP) ? "\x1b[5m DUP!\x1b[0m\n" : "\n"));
                // following the behaviour of ping, DUPs are not counted as lost packets
                break;
            case ICMP_TIME_EXCEEDED:
                fprintf(stdout, "time to live exceeded with ttl= %d code= %s\n",
                        ttl, icmp_time_exceeded_code[icmprep->code >= 2 ? 2 : icmprep->code]);
                break;
            case ICMP_DEST_UNREACH:
                fprintf(stdout, "destination host unreachable! code= %s\n",
                        icmp_dest_unreach_code[icmprep->code >= 6 ? 6 : icmprep->code]);
                break;
            default:
                break;
        }

chkf:
        fflush(stdout);
        sleepfn(RCVTIMEO);
incs:
        icmp->un.echo.sequence = htons(++seq);
    }
}

static
void siginth(int x){
    (void)(x);
    r = false;
}

static
void results(void){
    if (sentnum | recvnum)
        fprintf(stdout, "\nRESULTS:\n");
    if (sentnum && recvnum <= sentnum)
        fprintf(stdout, "sent=%lu received=%lu (%.1f%% packet loss.)\n",
                sentnum, recvnum,100 - (100 * recvnum / (float)sentnum));
    if (recvnum && ortt)
        fprintf(stdout, "min=%.1fms rtt=%.1fms max=%.1fms\n",
                lrtt / 1000.0, ortt / 1000.0, hrtt / 1000.0);
}

int main(const int argc, const char **argv){
    if (argc < 2){
        usage();
        exit(0);
    }

    for (int i = 2; i < argc;){
        if (argv[i][0] != '-' || strlen(argv[i]) != 2) { die: atexit(usage); ERROR("Unkown argument..."); }
        switch (argv[i][1]){
            case 'f':
                f = true;
                sleepfn = nullfn;
                ++i;
                break;
            case 't':
                ttl = atoi(argv[i + 1]);
                i += 2;
                break;
            default:
                goto die;
        }
    }

    struct sigaction new;
    new.sa_handler = siginth;
    new.sa_flags = 0;
    sigemptyset(&new.sa_mask);
    // NOTE: we can't use signal where because it fucks with the syscalls
    // and we lose a packet. Default to the Linus way of handling this
    // even tough is "wrong"
    // see: https://stackoverflow.com/a/3800915
    sigaction(SIGINT, &new, NULL); // handle ^C (ctrl-c)

    ping(inet_addr((dha = get_host_addr(argv[1]), atexit(results), dha)));

    return 0;
}
