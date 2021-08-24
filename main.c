// See LICENSE for license details.
#define _GNU_SOURCE // for NI_MAXHOST apparently
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
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
#include <linux/icmp.h>

#include "utils.c"

char *dha = NULL; // destiny host name

uint32_t ttl = 64;
uint64_t seq = 1;
uint64_t sentnum = 0;
uint64_t recvnum = 0;

suseconds_t hrtt = 0;
suseconds_t lrtt = 0x7FFFFFFF;
suseconds_t ortt = 0;
suseconds_t nrtt = 0;

uint8_t recvtable[MAXDUP / 8] = {0};

bool r = true; // running

bool f = false;

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

static
char *get_host_addr(const char *hname){
    static char host[NI_MAXHOST] = {0};

    struct addrinfo h;
    struct addrinfo *res;

    memset(&h, 0, sizeof(struct addrinfo));
    h.ai_family = AF_INET; // for ipv4

    uint32_t ret = getaddrinfo(hname, NULL, &h, &res);
    if (getaddrinfo(hname, NULL, &h, &res)){
        ERROR(gai_strerror(ret));
    }

    // get only the first address
    getnameinfo(res->ai_addr, res->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST);

    char *haddr = (char*)malloc(strlen(host) + 1);
    strcpy(haddr, host);
    freeaddrinfo(res);

    host[0] = '\0'; // don't think we need to do this, but better be safe

    return haddr;
}

static
int initsock(pid_t pid){
    int socketd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socketd < 0){ ERROR("Fail to create a socket, run with root privileges..."); }

    int on = 1;
	if (setsockopt(socketd, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) == -1){
        ERROR("setsockopt IP_HDRINCL");
	}

    struct timeval timeo;
    timeo.tv_sec = f ? 0 : RCVTIMEO;
    timeo.tv_usec = 0;
    if (setsockopt(socketd, SOL_SOCKET, SO_RCVTIMEO, &timeo, sizeof(timeo)) == -1){
        ERROR("setsockopt SO_RCVTIMEO");
    }
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

void ping(const in_addr_t daddr, const uint8_t ttl){
    size_t packetsize = sizeof(struct iphdr) + sizeof(struct icmphdr) + PAYLOADSIZE;

    uint8_t *packetreq = calloc(sizeof(uint8_t), packetsize);
    uint8_t *packetrep = calloc(sizeof(uint8_t), packetsize);
    if (!packetreq || !packetrep) { ERROR("Fail to allocate memory..."); }

    static const size_t icmpoff = sizeof(struct iphdr);
    static const size_t timeoff = icmpoff + sizeof(struct icmphdr);

    struct iphdr *ip = (struct iphdr*)packetreq;
    struct icmphdr *icmp = (struct icmphdr*)(packetreq + icmpoff);
    struct timeval *timestamp = (struct timeval*)(packetreq + timeoff);

    struct iphdr *iprep = (struct iphdr*)packetrep;
    struct icmphdr *icmprep = (struct icmphdr*)(packetrep + icmpoff);
    struct timeval *timestamprep = (struct timeval*)(packetrep + timeoff);

    pid_t pid;
    int socketd = initsock((pid = getpid()));
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(PAYLOADSIZE);
    ip->id = htonl(pid);
    ip->protocol = IPPROTO_ICMP;
    ip->ttl = ttl;
    ip->daddr = daddr;

    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.sequence = htons((uint16_t)seq);
    icmp->un.echo.id = htonl(pid);

    struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = daddr;
	memset(&servaddr.sin_zero, 0, sizeof(servaddr.sin_zero));

    int32_t sentsize = 0;
    int32_t recvsize = 0;

    fprintf(stdout, HDRFMT, dha, PAYLOADSIZE, packetsize); fflush(stdout);

    while (r){
send:
        recvtable[((sentnum + 1) % MAXDUP) >> 3] &= ~(1 << (((sentnum + 1) % MAXDUP) & 0x07));
        gettimeofday(timestamp, NULL);
        icmp->checksum = 0;
        icmp->checksum = checksum((uint16_t*)icmp, sizeof(struct icmphdr) + PAYLOADSIZE);
        // TODO: replace sendto and recvfrom with sendmsg and recvmsg :)
        if ((sentnum++, sentsize = sendto(socketd, packetreq, packetsize, 0,
                        (struct sockaddr*)&servaddr, sizeof(servaddr))) < 1){
            goto send; // if we can't send keep trying.
        }

        if ((recvsize = recvfrom(socketd, packetrep, packetsize, MSG_WAITALL, NULL, NULL)) == -1 && !(errno & EINTR)){
            fprintf(stderr, TMOFMT, RCVTIMEO, ntohs(icmp->un.echo.sequence)); fflush(stderr);
            goto send; // we didn't get a reply in time, send again with the same seq
        }
        else if (errno & EINTR){
            --sentnum; // if we got were recvfrom was interrupted, ignore the last package we sent
            break;
        }

        // TODO: check the checksum!

        switch ((++recvnum, icmprep->type)){
            case ICMP_ECHOREPLY:
                calcrtt(timestamprep);
                fprintf(stdout, OUTFMT, recvsize, dha, ntohs(icmprep->un.echo.sequence), iprep->ttl, nrtt/1000.0);
                fprintf(stdout, (checkdup(htons(icmprep->un.echo.sequence) % MAXDUP) ? DUPFMT: "\n"));
                // following the behaviour of ping, DUPs are not counted as lost packets.
                break;
            case ICMP_TIME_EXCEEDED:
                // TODO:
                break;
            case ICMP_DEST_UNREACH:
                // TODO:
                break;
            default:
                break;
        }

        fflush(stdout);
        icmp->un.echo.sequence = htons(++seq);

        sleepfn(1);
    }
}

static
void siginth(int x){
    (void)(x);
    r = false;
}

static
void results(void){
    if (sentnum | recvnum) fprintf(stdout, RESFMT0);
    if (sentnum) fprintf(stdout, RESFMT1, sentnum, recvnum,100 - (100 * recvnum / (float)sentnum));
    if (recvnum & ortt) fprintf(stdout, RESFMT2, lrtt / 1000.0, ortt / 1000.0, hrtt / 1000.0);
}

int main(const int argc, const char **argv){
    if (argc < 2){
        usage();
        exit(0);
    }

    for (int i = 2; i < argc; i+=2){
        if (argv[i][0] != '-' && strlen(argv[i]) != 2) { die: atexit(usage); ERROR("Unkown argument..."); }
        switch (argv[i][1]){
            case 'f':
                f = true;
                sleepfn = nullfn;
                break;
            case 't':
                ttl = atoi(argv[i + 1]);
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
    // even tough is "wrong".
    // see: https://stackoverflow.com/a/3800915
    sigaction(SIGINT, &new, NULL); // handle ^C (ctrl-c)

    ping(inet_addr((dha = get_host_addr(argv[1]), atexit(results), dha)), ttl);

    return 0;
}
