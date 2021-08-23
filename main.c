// See LICENSE for license details.
#define _GNU_SOURCE // for NI_MAXHOST apparently
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
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

char *dha = NULL; // destiny host name

uint16_t seq = 1;
uint32_t ttl = 64;
uint64_t sentnum = 0;
uint64_t recvnum = 0;

suseconds_t hrtt = 0;
suseconds_t lrtt = 0xEFFFFFFF;
suseconds_t ortt = 0;
suseconds_t nrtt = 0;

volatile bool r = true; // running

static
void usage(void){
    fprintf(stderr,
            "usage:\n"
            "\t./pong [target] [-f] [-t time_to_live]\n");
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

char *get_host_addr(const char *hname){
    static char host[NI_MAXHOST];

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

// mock function, avoid calling sleep with 0
static inline
uint32_t nullfn(uint32_t){
    return 0;
}

#define SLEEPTIME 1
#define PAYLOADSIZE 56
#define RCVTIMEOUT 1

#define HDRFMT "PONGING %s %d bytes of data (%lu total).\n"
#define OUTFMT "%d bytes from %s icmp_seg=%d ttl=%u time=%.1fms"
#define TMOFMT "timeout(%ds) for icmp_seq=%d\n"

void ping(const in_addr_t daddr, const uint8_t ttl, uint32_t(*sleepfn)(uint32_t)){
    size_t packetsize = sizeof(struct iphdr) + sizeof(struct icmphdr) + PAYLOADSIZE;

    uint8_t *packetreq = calloc(sizeof(uint8_t), packetsize);
    uint8_t *packetrep = calloc(sizeof(uint8_t), packetsize);
    if (!packetreq || !packetrep) {
        ERROR("Fail to allocate memory...");
    }

    struct iphdr *ip = (struct iphdr*)packetreq;
    struct icmphdr *icmp = (struct icmphdr*)(packetreq + sizeof(struct iphdr));
    struct timeval *timestamp = (struct timeval*)(packetreq + sizeof(struct iphdr) + sizeof(struct icmphdr));

    struct iphdr *iprep = (struct iphdr*)packetrep;
    struct icmphdr *icmprep = (struct icmphdr*)(packetrep + sizeof(struct iphdr));
    struct timeval *timestamprep = (struct timeval*)(packetrep + sizeof(struct iphdr) + sizeof(struct icmphdr));

    int socketd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socketd < 0){
        ERROR("Fail to create a socket, run with root privileges...");
    }

    int on = 1;
	if (setsockopt(socketd, IPPROTO_IP, IP_HDRINCL, 
                (const char*)&on, sizeof (on)) == -1) {
        ERROR("setsockopt IP_HDRINCL");
	}
    if (setsockopt(socketd, SOL_SOCKET, SO_BROADCAST, 
                (const char*)&on, sizeof(on)) == -1){
        ERROR("setsockopt SO_BROADCAST");
    }
    struct timeval timeo;
    timeo.tv_sec = RCVTIMEOUT;
    timeo.tv_usec = 0;
    if(setsockopt(socketd, SOL_SOCKET, SO_RCVTIMEO,
                (const char*)&timeo, sizeof(timeo)) == -1){
        ERROR("setsockopt SO_RCVTIMEO");
    }

    pid_t pid = getpid();

    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(PAYLOADSIZE);
    ip->id = htonl(pid);
    ip->ttl = ttl;
    ip->protocol = IPPROTO_ICMP;
    ip->daddr = daddr;

    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.sequence = htons(seq);
    icmp->un.echo.id = htonl(pid);

    struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = daddr;
	memset(&servaddr.sin_zero, 0, sizeof (servaddr.sin_zero));

    int32_t sentsize = 0;
    int32_t recvsize = 0;

    struct timeval timenow;
    struct timeval timedif;

    fprintf(stdout, HDRFMT, dha, PAYLOADSIZE, packetsize);
    fflush(stdout);
    while (r){
send:
        gettimeofday(timestamp, NULL);
        icmp->checksum = 0;
        icmp->checksum = checksum((uint16_t*)icmp, sizeof(struct icmphdr) + PAYLOADSIZE);
        // TODO: replace sendto and recvfrom with sendmsg and recvmsg :)
        if ((sentnum++, sentsize = sendto(socketd, packetreq, packetsize, 0, 
                        (struct sockaddr*)&servaddr, sizeof(servaddr))) < 1){
            sleepfn(SLEEPTIME);
            // fprintf(stderr,"Fail to pong, check your connection...");
            // fflush(stderr);
            goto send; // if we can't send keep trying.
        }

        if ((recvsize = recvfrom(socketd, packetrep, packetsize, 0, NULL, NULL)) == -1 && errno != EINTR){
            fprintf(stderr, TMOFMT, RCVTIMEOUT, ntohs(icmprep->un.echo.sequence));
            fflush(stderr);
            goto send; // we didn't get a reply in time, send again with the same seq
        }
        else if (errno & EINTR){
            continue;
        }

        recvnum++;

        gettimeofday(&timenow, NULL);

        // TODO: check for ECHO_REPLY codes
        // TODO: check the checksum?

        timersub(&timenow, timestamprep, &timedif); // use the time stored in the packet
        nrtt = timedif.tv_sec * 10000 + timedif.tv_usec;
        if (hrtt < nrtt) hrtt = nrtt;
        if (lrtt > nrtt) lrtt = nrtt;
        if (!ortt)
            ortt = nrtt;
        else
            ortt = ((7/8.) * (float)ortt) + ((float)nrtt * (1/8.));

        fprintf(stdout, OUTFMT, recvsize, dha, ntohs(icmprep->un.echo.sequence), iprep->ttl, nrtt/1000.0);
        fprintf(stdout, (htons(icmprep->un.echo.sequence) < seq ? "DUP\n" : "\n")); 
        // if the packet seq is lower than seq signal a DUP
        // following the behaviour of ping, DUPs are not counted
        // as lost packets.
        fflush(stdout);

        sleepfn(SLEEPTIME);

        icmp->un.echo.sequence = htons(++seq);
    }
}

static
void siginth(int){
    r = false;
    fprintf(stdout,"\b\b"); // erase ^C
    fflush(stdout);
}

#define RESFMT                                     \
    "RESULTS:\n"                                   \
    "sent=%lu received=%lu (%.1f%% packet loss)\n" \
    "min=%.1fms rtt=%.1fms max=%.1fms\n"
static
void results(void){
    fprintf(stdout, RESFMT, sentnum, recvnum,100 - (100  * sentnum / (float)recvnum),
            lrtt / 1000.0, ortt / 1000.0, hrtt / 1000.0);
}

int main(const int argc, const char **argv){
    if (argc != 2){
        usage();
        exit(0);
    }

    // TODO: parse the args

    struct sigaction new;
    new.sa_handler = siginth;
    new.sa_flags = 0;
    sigemptyset(&new.sa_mask);
    // NOTE: we can't use signal where because it fucks with the syscalls
    // and we lose a packet. Default to the Linus way of handling this
    // even tough is "wrong".
    // see: https://stackoverflow.com/a/3800915
    sigaction(SIGINT, &new, NULL); // handle ^C (ctrl-c)

    atexit(results);

    ping(inet_addr((dha = get_host_addr(argv[1]))), ttl, sleep);

    return 0;
}
