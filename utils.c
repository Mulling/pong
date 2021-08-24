// See LICENSE for license details.
#define MAXDUP 0x1000 // from ping.c
#define PAYLOADSIZE 56
#define RCVTIMEO 1

#define HDRFMT "PONGING %s %d bytes of data (%lu total).\n"
#define OUTFMT "%d bytes from %s icmp_seq=%d ttl=%u time=%.1fms"
#define TMOFMT "timeout(%ds) for icmp_seq=%d\n"
#define DUPFMT "\x1b[5m DUP!\x1b[0m\n"
#define EXCFMT "time to live exceeded with ttl= %d\n"
#define UNRFMT "destination host unreachable!\n"
#define RESFMT0 "\nRESULTS:\n"
#define RESFMT1 "sent=%lu received=%lu (%.1f%% packet loss.)\n"
#define RESFMT2 "min=%.1fms rtt=%.1fms max=%.1fms\n"

#ifdef DEBUG
#define ERROR(msg)                                                  \
        fprintf(stderr, __FILE__":%d error: %s \n", __LINE__, msg); \
        exit(1);
#else
#define ERROR(msg)                                 \
        fprintf(stderr, "pong: error: %s\n", msg); \
        exit(1);
#endif
