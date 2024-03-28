#include <zephyr/logging/log.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(ipsp);

/* Preventing log module registration in net_core.h */
#define NET_LOG_ENABLED 0
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/udp.h>
#include <sh1106_gfx.h>
#include <net_ble.h>
#include <time.h>

struct net_context *udp_recv6 = {0};
struct sockaddr dst_addr;

#define MY_PORT 4242

#define STACKSIZE 2000
K_THREAD_STACK_DEFINE(thread_stack, STACKSIZE);
static struct k_thread thread_data;

static uint8_t buf_tx[NET_IPV6_MTU];

#define MAX_DBG_PRINT 64

static struct k_sem quit_lock;

static inline void quit(void)
{
    k_sem_give(&quit_lock);
}

static inline bool get_context(struct net_context **udp_recv6)
{
    int ret;
    struct sockaddr_in6 my_addr6 = {0};

    my_addr6.sin6_family = AF_INET6;
    my_addr6.sin6_port = htons(MY_PORT);

    ret = net_context_get(AF_INET6, SOCK_DGRAM, IPPROTO_UDP, udp_recv6);
    if (ret < 0)
    {
        LOG_ERR("Cannot get network context for IPv6 UDP (%d)", ret);
        return false;
    }

    ret = net_context_bind(*udp_recv6, (struct sockaddr *)&my_addr6,
                           sizeof(struct sockaddr_in6));
    if (ret < 0)
    {
        LOG_ERR("Cannot bind IPv6 UDP port %d (%d)",
                ntohs(my_addr6.sin6_port), ret);
        return false;
    }

    return true;
}

static int build_reply(const char *name,
                       struct net_pkt *pkt,
                       uint8_t *buf)
{
    int reply_len = net_pkt_remaining_data(pkt);
    int ret;

    LOG_DBG("%s received %d bytes", name, reply_len);

    memset(buf, 0, 32);
    ret = net_pkt_read(pkt, buf, reply_len);
    printk("Received:%s\n", buf);
    buf[reply_len + 1] = 0;

    if (ret < 0)
    {
        LOG_ERR("cannot read packet: %d", ret);
        return ret;
    }

    LOG_DBG("sending %d bytes", reply_len);

    return reply_len;
}

static inline void pkt_sent(struct net_context *context,
                            int status,
                            void *user_data)
{
    if (status >= 0)
    {
        LOG_DBG("Sent %d bytes", status);
    }
}

static inline void set_dst_addr(sa_family_t family,
                                struct net_pkt *pkt,
                                struct net_ipv6_hdr *ipv6_hdr,
                                struct net_udp_hdr *udp_hdr,
                                struct sockaddr *dst_addr)
{
    net_ipv6_addr_copy_raw((uint8_t *)&net_sin6(dst_addr)->sin6_addr,
                           ipv6_hdr->src);
    net_sin6(dst_addr)->sin6_family = AF_INET6;
    // if (ntohs(udp_hdr->src_port) == 4242)
    //  net_sin6(dst_addr)->sin6_port = udp_hdr->src_port;
    //  else
    net_sin6(dst_addr)->sin6_port = htons(5859); // udp_hdr->src_port;
}

static void udp_received(struct net_context *context,
                         struct net_pkt *pkt,
                         union net_ip_header *ip_hdr,
                         union net_proto_header *proto_hdr,
                         int status,
                         void *user_data)
{
    printk("Received from Address:");
    for (uint8_t i = 0; i < 16; i++)
        printk("%x ", ip_hdr->ipv6->src[i]);
    printk("\n");

    sa_family_t family = net_pkt_family(pkt);
    static char dbg[MAX_DBG_PRINT + 1];
    int ret;

    snprintf(dbg, MAX_DBG_PRINT, "UDP IPv%c",
             family == AF_INET6 ? '6' : '4');

    set_dst_addr(family, pkt, ip_hdr->ipv6, proto_hdr->udp, &dst_addr);

    ret = build_reply(dbg, pkt, buf_tx);
    if (ret < 0)
    {
        LOG_ERR("Cannot send data to peer (%d)", ret);
        return;
    }
    
    net_pkt_unref(pkt);

    if (proto_hdr->udp->src_port == htons(4242)) // Donot reply to the ack packets
    {
        printk("Returning!!!\n");
        return;
    }

    ret = net_context_sendto(context, "Acknowledgement from nRF52 BLE 6LoWPAN Node-2.", 46, &dst_addr,
                             family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in),
                             pkt_sent, K_NO_WAIT, user_data);
    if (ret < 0)
    {
        LOG_ERR("Cannot send data to peer (%d)", ret);
    }
}

static void net_udp_setup_callback(struct net_context *udp_recv6)
{
    int ret;

    ret = net_context_recv(udp_recv6, udp_received, K_NO_WAIT, NULL);
    if (ret < 0)
    {
        LOG_ERR("Cannot receive IPv6 UDP packets");
    }
}

void net_udp_send(char *msg, int len, struct sockaddr *destination_addr)
{
    int ret;
    if (destination_addr == NULL)
    {
        ret = net_context_sendto(udp_recv6, msg, len, &dst_addr,
                                 sizeof(struct sockaddr_in6),
                                 pkt_sent, K_NO_WAIT, NULL);
    }
    else
    {
        ret = net_context_sendto(udp_recv6, msg, len, destination_addr,
                                 sizeof(struct sockaddr_in6),
                                 pkt_sent, K_NO_WAIT, NULL);
    }

    if (ret < 0)
    {
        LOG_ERR("Cannot send data to peer (%d)", ret);
    }
}

static void listen(void)
{

    if (!get_context(&udp_recv6))
    {
        LOG_ERR("Cannot get network contexts");
        return;
    }

    LOG_INF("Starting to wait");

    net_udp_setup_callback(udp_recv6);

    k_sem_take(&quit_lock, K_FOREVER);

    LOG_INF("Stopping...");

    net_context_put(udp_recv6);
}

void net_ble_init(void)
{
    LOG_INF("Run IPSP sample");

    k_sem_init(&quit_lock, 0, K_SEM_MAX_LIMIT);

    k_thread_create(&thread_data, thread_stack, STACKSIZE,
                    (k_thread_entry_t)listen,
                    NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
}
